/** \file investment.cpp
	\brief Implementation of the investment model, using Smi and FlopC++
	\author Michal Kaut

	This version uses FlopC++ to construct the core model and then Smi to
	construct the scenario-tree structure (using FlopC++ to get the relevant
	column and row indices). It is basically a combination of the Smi and FlopC++
	(with stage-node objects) examples. Note that we use the stage-node objects
	directly from the FlopC++ example, which means that they are more general
	than needed in this case (where we only construct a deterministic model, so
	all non-leaf nodes only have one child).

	Note that the code is meant as an illustrative example that mixes different
	styles to show more ways of doing things, something you most likely do \e not
	want to do in a real code. In addition, in a real code one would probably
	make the members private and write get/set methods where needed. This has
	been omitted here to make the example shorter.
*/

#include <iomanip>
#include "SmiScnModel.hpp"
#include "flopc.hpp"

// Change these two lines to use a different solver
#include "OsiClpSolverInterface.hpp"
#define OSI_SOLVER_INTERFACE OsiClpSolverInterface

using namespace std;
using namespace flopc;

/// This is the generic base class for describing a node.
class StageNodeBase {
public:

		StageNodeBase *ptParent;      ///< pointer to parent node
		StageNodeBase *ptChild;       ///< pointers to children of this node
		MP_expression objFunction;    ///< objective function at this node

		/// \name References to variables and constraints
		/** The idea is to have "meta objects" with all variables and constraints
		    in a node. This is important in creation of the Smi object, where we
		    have to associate the nodes variables and constraints to stages.
		    Without these new objects, we would have to access all the derived
		    classes independently, cluttering the code. **/
		///@{
		vector<VariableRef *>   all_variables;     ///< references to all variables
		vector<MP_constraint *> all_constraints;   ///< references to all constraints


		StageNodeBase(StageNodeBase *ptPred)
		:  ptParent(ptPred), ptChild(NULL)
		{
			if (ptParent != NULL)
				ptParent->ptChild = this;           // Register with the parent
		}
		virtual ~StageNodeBase() {
			for (int a = 0; a < (int) all_variables.size(); a++) {
				delete all_variables[a];
			}
		}
protected:

		/// Create the objective function expression, recursively for all children
		/** This function is protected, as it only makes sense to call it in
		    the root, to create the complete objective function. **/
		virtual void make_obj_function_() {
			assert (ptChild != NULL && "Leaves should never call this function");
			// Create the objective recursively for all descendants of the node
			if (ptChild)
				ptChild->make_obj_function_();
			objFunction = ptChild->objFunction;
		}
};


/// This is the base class for all stage models.
/** Note that variable \a x, as well as the vector of \a children, should not
    really be in this class, as they are not needed in all the nodes (they
    do not exist in leaves). It would thus be better to have an additional
    derived class for non-leaf nodes and then derive RootNode and MidStageNode
    from this one. This solution, however, brings some other problems... */
class StageNode : public StageNodeBase
{
	public:
		MP_set ASSETS;                ///< set of assets
		MP_index a;                   ///< index used in formulas
		MP_variable x;                ///< the "buy" variable, defined on ASSETS
		MP_variable wealth;           ///< the wealth at each period
		MP_constraint wealth_defn;    ///< the equation defining wealth

		StageNode(StageNode *ptPred, const int nmbAssets)
				  : StageNodeBase(ptPred),ASSETS(nmbAssets), x(ASSETS)
		{
			wealth_defn = sum(ASSETS(a), x(a)) == wealth();

			all_variables.push_back(new VariableRef(wealth()));
			for (int a = 0; a < nmbAssets; a++) {
				all_variables.push_back(new VariableRef(x(a)));
			}
            all_constraints.push_back(&wealth_defn);
		}

		/// getParent() function.
		/** The parent is a base class object.  To access the members ASSETS and x()
		    we need to cast it to the derived class. **/
		StageNode *getParent() {return (StageNode *)ptParent;}

		virtual ~StageNode() {}

		/// A common way to access the balance constraints in the derived classes
		/** Two of the derived classes have a cash-flow balance constraint with
		    the \a Return values in it. These constraints have to be accessed when
		    creating scenarios and without this common handle, we would need to
		    write a separate code for the two classes. **/
		MP_constraint *balance_constraint;
		///@}


		/// get the wealth at the nodes
		/** We cannot use x.level(), because this is linked to the core model,
		    not the stochastic model. Instead, we provide the function with the
		    current values of the node's variables. Note that in general, we might
		    have to provide the values of stochastic data as well. <br>
		    By default, the wealth is the sum of all the \a x variables, so it must
		    be overridden in the leaves. **/
		virtual double get_wealth(const double *variableValues, const int nmbVars) {
			assert (nmbVars == (int) all_variables.size()
			        && "Check that we have values of all variables");
			double w = 0;
			for (int i = 0; i < ASSETS.size(); i++)
			{
				int j = x(i).getColumn();
				w += variableValues[j];
			}
			assert(w == variableValues[wealth().getColumn()]
			                                && "Wealth should equal sum of position values.");
			return variableValues[wealth().getColumn()];
		}


		virtual void loadModifiedMatrix(CoinPackedMatrix &ADiff, double *retData)
		{
			// Find row and column number of the matrix elements with Return values
			int i = balance_constraint->row_number();
			for (int a=0; a<ASSETS.size(); a++) {
				// Returns are on 'x' variables from the parent!
				int j = this->getParent()->x(a).getColumn();
				ADiff.modifyCoefficient(i, j, retData[a]);
			}
		}

};

/// Class for the root node of the tree
class RootNode : public StageNode {
	public:
		MP_constraint initialBudget; ///< initial budget constraint

		RootNode(const int nmbAssets, const double initWealth)
		: StageNode(NULL, nmbAssets)
		{
			initialBudget() = wealth() == initWealth;

			all_constraints.push_back(&initialBudget);
			balance_constraint = NULL;
		}

		/// This is the public interface to the protected \c make_obj_function_()
		/** This is to prevent the user to call \c make_obj_function_() for other
		    nodes than the root, which does not make sense. **/
		void make_objective_function() {
			make_obj_function_();
		}
};

/// Class for the middle-nodes, i.e. all the nodes between the root and leaves
class MidStageNode : public StageNode {
	public:
		MP_constraint cashFlowBalance; ///< cash-flow balance constraint
		MP_data Return;                ///< returns of the assets at this node

		/// Constructs a \c MidStageNode object
		/** Here we use a <em>shallow copy</em> for the \c MP_data \a Return,
		    i.e. the return values in the constraints will be linked to the array
		    \a retVect. This means that if the external array changes before we
		    build the OSI object (using the \c attach method), the constraints will
		    be changed as well - and if the external array is deallocated, the
		    program will crash on calling the \c attach method! **/
		MidStageNode(StageNode *ptPred, double *ptRetVect)
		: StageNode(ptPred, ptPred->ASSETS.size()),
		  Return(ptRetVect, ASSETS)
		{
			// This shows the use of MP_index in a formula
			cashFlowBalance = sum(ASSETS(a), getParent()->x(a) * Return(a)) == wealth();

			all_constraints.push_back(&cashFlowBalance);
			balance_constraint = &cashFlowBalance;
		}
};

/// This is the class for the leaves, i.e. the last-stage nodes
class LeafNode : public MidStageNode {
	public:
		MP_variable w;              ///< shortage variable
		MP_variable y;              ///< surplus variable
		MP_constraint penalty;      ///< equation defining the surplus and shortage

		/// Constructs a \a LeafNode object
		/** A LeafNode is a MidStageNode with a penalty for the capital target. **/
		LeafNode(StageNode *ptPred, double *ptRetVect, const double capTarget)
		: MidStageNode(ptPred, ptRetVect)
		{
			penalty = wealth() + w() - y() == capTarget;

			all_variables.push_back(new VariableRef(w()));
			all_variables.push_back(new VariableRef(y()));
			all_constraints.push_back(&penalty);
		}
	protected:
		/// version of \a make_obj_function_() for the leaves - no recursion
		void make_obj_function_() {
			objFunction = 1.3 * w() - 1.1 * y();
		}
};


// -------------------------------------------------------------------------- //

/// Class describing the scenario-tree structure
class ScenTreeStruct {
	public:
		int nmbNodes;           ///< nodes are 0..nmbNodes-1, where 0 is root
		int firstLeaf;          ///< nodes firstLeaf..nmbNodes-1 are leaves

		/// Constructs the object
		ScenTreeStruct(const int nNodes, const int firstL)
		: nmbNodes(nNodes), firstLeaf(firstL)
		{}
		virtual ~ScenTreeStruct(){}

		/// Get the parent of a given node
		/** In a general case, this would be given by a table, for balanced trees
		    one can use a simple formula. The question is what to do with the root:
		    should the function return 0, -1, or throw an exception? **/
		virtual int get_parent(int n) const = 0;

		/// Get the number of stages
		virtual int get_nmb_stages() const = 0;

};

/// Class for balanced binary trees
class BinTreeStruct : public ScenTreeStruct {
	protected:
		int nmbStages;
		int * scenNodeNmb;
		int nextLeaf;


	public:
		/// Constructs the object - 2^T-1 nodes, first leaf is 2^(T-1)-1
		BinTreeStruct(const int T)
		: ScenTreeStruct((int) pow(2.0, T) - 1, (int) pow(2.0, T-1) - 1),
			nmbStages(T)
		{
			scenNodeNmb = new int[T];
			nextLeaf = this->firstLeaf;
		}
		~BinTreeStruct(){
			delete scenNodeNmb;
		}

		int get_parent(int n) const {
			return (n-1) / 2;    // This gives: get_parent(0) = 0
		}

		int get_nmb_stages() const {
			return nmbStages;
		}

		int * getCoreScenario(){
			int n = this->firstLeaf;
			for (int t = nmbStages; t > 0; t--) {
				scenNodeNmb[t-1] = n;
				n = this->get_parent(n);
			}
			return scenNodeNmb;
		}

		int * getNextScenario(int *scen, int* parentScen, int *branchStage, double *prob){
			if (nextLeaf == nmbNodes)
				return NULL;
			int n = nextLeaf;
			int t = nmbStages-1;
			// For each scenario, start by adding the leaf and then go up, as long as
			// the nodes are different from the previous (parent) scenario
			while (n != scenNodeNmb[t]) {
				assert (n > 0 && t > 0 && "All scenarios must end in a common root");
				scenNodeNmb[t] = n; // add the current node to the list
				n = this->get_parent(n);
				t--;
			}
			*scen = nextLeaf - this->firstLeaf;         // scenario index
			*parentScen = (*scen == 0 ? 0 : *scen - 1); // parent scenario
			*branchStage = (*scen == 0 ? 1 : t+1);      // branching scenario
		 	*prob = 1.0 / this->getNmbScen();           // equiprobable scen.
		 	nextLeaf++;
			return scenNodeNmb;
		}

		inline int getNmbScen()	{
			return this->nmbNodes - this->firstLeaf;
		}
};


// -------------------------------------------------------------------------- //

int main()
{
	// DATA - This would be normally read from some external file

	// binary scenario tree with 4 stages: 15 nodes, firstLeaf = 7
	const int nmbStages = 4;
	BinTreeStruct scTree(nmbStages);

	// model parameters
	enum {stocks, bonds, nmbAssets}; // assets to invest to; sets nmbAssets = 2
	double InitBudget = 55;          // initial budget
	double CapTarget = 80;           // capital target

	// vector of returns at the 14 non-root nodes
	double retData[][nmbAssets] = {
		{1.25, 1.14},
		{1.06, 1.16},
		{1.21, 1.17},
		{1.07, 1.12},
		{1.15, 1.18},
		{1.06, 1.12},
		{1.26, 1.13},
		{1.07, 1.14},
		{1.25, 1.15},
		{1.06, 1.12},
		{1.05, 1.17},
		{1.06, 1.15},
		{1.05, 1.14},
		{1.06, 1.12}
	};


	// ------------------------------------------------------------------------ //
	//                       CREATE THE CORE OBJECT

	// Initialize the object for the core (deterministic) model
	MP_model &coreModel = MP_model::getDefaultModel();
	coreModel.setSolver(new OSI_SOLVER_INTERFACE);
	coreModel.verbose(); // less output

	int i, j, t;
	assert (nmbStages == scTree.get_nmb_stages()
	        && "Checking that get_nmb_stages() returns what it should.");

	// Get the node numbers for the Core
	int * scenNodeNmb = scTree.getCoreScenario();

	// Create scenario tree for the core model, using data for the 1st scenario
	vector<StageNode *> coreNodes(nmbStages);
	coreNodes[0] = new RootNode(nmbAssets, InitBudget);
	for (t = 1; t < nmbStages-1; t++) {
		coreNodes[t] = new MidStageNode(coreNodes[t-1],
		                                retData[scenNodeNmb[t]-1]);
	}
	assert (t == nmbStages-1 && "t should be nmbStages-1 after the loop");
	coreNodes[t] = new LeafNode(coreNodes[t-1], retData[scenNodeNmb[t]-1],
	                            CapTarget);

	// create a "shortcut object" for the root
	RootNode *ptRoot = dynamic_cast<RootNode *>(coreNodes[0]);

	ptRoot->make_objective_function();           // Create the objective func.
	coreModel.setObjective(ptRoot->objFunction); // Set the objective
	coreModel.attach();                          // Attach the model

	// Get number of variables and constraints from the OSI model
	// Remember that the "->" operator on an MP_model returns the OSI model
	int nmbCoreCols = coreModel->getNumCols();
	int nmbCoreRows = coreModel->getNumRows();

	// Write an MPS file + print some info
	coreModel->writeMps("investment.core");
	cout << endl << "The core (deterministic) model has " << nmbCoreCols
	     << " variables and " << nmbCoreRows << " constraints." << endl;

	// Now, we have to get the stage number for all variables and constraints
	// Note that this can be done first after we have attached the model!
	int *colStages = new int[nmbCoreCols];
	int checkSum = 0; // sum of the column numbers, for checking (see later)
	for (t = 0; t < nmbStages; t++) {
		for (j = 0; j < (int) coreNodes[t]->all_variables.size(); j++) {
			int colIndx = coreNodes[t]->all_variables[j]->getColumn();
			#ifndef NDEBUG
			cout << "stage " << t << ": var no. " << j+1 << " is in column "
			     << colIndx << endl;
			#endif
			colStages[colIndx] = t;
			checkSum += colIndx;
		}
	}
	// Finished -> check the sum
	assert (checkSum == nmbCoreCols * (nmbCoreCols-1) / 2
	        && "checkSum = sum of numbers from zero to nmbCoreCols-1");

	// Now do the same for the constraints
	int *rowStages = new int[nmbCoreRows];
	checkSum = 0;
	for (t = 0; t < nmbStages; t++) {
		for (i = 0; i < (int) coreNodes[t]->all_constraints.size(); i++) {
			int rowIndx = *coreNodes[t]->all_constraints[i];
			#ifndef NDEBUG
			cout << "stage " << t << ": constraint no. " << i+1 << " is in row "
			     << rowIndx << endl;
			#endif
			rowStages[rowIndx] = t;
			checkSum += rowIndx;
		}
	}
	// Finished -> check the sum
	assert (checkSum == nmbCoreRows * (nmbCoreRows-1) / 2
	        && "checkSum = sum of numbers from zero to nmbCoreRows-1");

	// Now, we can build the CORE problem, i.e. the deterministic version
	SmiCoreData stochCore(coreModel.operator->(), nmbStages,
	                      colStages, rowStages);
	delete[] colStages;
	delete[] rowStages;


	// ------------------------------------------------------------------------ //
	//               START BUILDING THE STOCHASTIC MODEL

	// This is done in an SMPS-like fashion, ie. each scenario has a parent
	// scenario it branches from. We then have to specify the branching stage
	// and all the data that are different than the parent's.
	// In our case, the only difference is in the matrix A. We only need to
	// specify the elements that differ from the parent, that is the returns.
	SmiScnModel stochModel;

	// the matrix of differences wrt. the previous (parent) scenario
	CoinPackedMatrix ADiff;
	// The default constructor creates a column-ordered matrix, while Smi uses
	// row-ordering; it would be done automatically later, but this is faster...
	ADiff.reverseOrdering();

	// Add scenarios, one by one.
	int scen,parentScen,branchStage;
	double scenProb;
	while(scenNodeNmb = scTree.getNextScenario(&scen,&parentScen,&branchStage,&scenProb)){
		ADiff.clear(); // clean the matrix of differences - must reset dimensions!
		ADiff.setDimensions(nmbCoreRows, nmbCoreCols);
		cout << "Nodes in scenario " << scen + 1 << ": ";
		for (t=branchStage; t<nmbStages; t++){
				cout << setw(2) << scenNodeNmb[t] << " ";
				// load modified data into ADiff
				coreNodes[t]->loadModifiedMatrix(ADiff,retData[scenNodeNmb[t]-1]);
		}
		cout << endl;

		#ifdef NDEBUG
			stochModel.generateScenario(&stochCore, &ADiff,
					NULL, NULL, NULL, NULL, NULL,
					branchStage, parentScen, scenProb);
		#else
			int scenIndx = stochModel.generateScenario(&stochCore, &ADiff,
					NULL, NULL, NULL, NULL, NULL,
					branchStage, parentScen,scenProb);
			assert (scenIndx == scen && "Check index of the new scenario");
		#endif
	}

	// ------------------------------------------------------------------------ //
	/* Now, the stochastic model is complete.
	   Unfortunately, there is no stochastic solver in COIN-OR yet, so we have to
	   solve the model using a deterministic solver on the determ. equivalent. */

	// Attach a solver to the stochastic model
	stochModel.setOsiSolverHandle(*new OSI_SOLVER_INTERFACE());

	// 'loadOsiSolverData()' loads the deterministic equivalent into an
	// internal OSI data structure and returns a handle (pointer) to it.
	OsiSolverInterface *ptDetEqModel = stochModel.loadOsiSolverData();

	// 'ptDetEqModel' now includes the deterministic equivalent
	ptDetEqModel->writeMps("investment.det-equiv");
	cout << endl << "Solving the deterministic equivalent:" << endl;
	ptDetEqModel->initialSolve();
	cout << endl << "The deterministic equivalent model has "
	     << ptDetEqModel->getNumCols() << " variables and "
	     << ptDetEqModel->getNumRows() << " constraints." << endl;
	cout << "Optimal objective value = " << ptDetEqModel->getObjValue() << endl;


	// ------------------------------------------------------------------------ //
	/* Even if we use a deterministic solver, we can still get information about
	   the solution on the scenario tree, using the SMI (stochastic) model */
	cout << endl << "The stochastic model has " << stochModel.getNumScenarios()
	     << " scenarios." << endl;
	assert (stochModel.getNumScenarios() == scTree.getNmbScen() && "Check number of scens.");

	// We will report the wealth at each node of the tree, plus the obj. value
	vector<double> nodeWealth(nmbStages, 0);
	double objValue = 0.0;

	// Compute the wealth at each node, by traversing the tree from leafs up
	for (SmiScenarioIndex sc = 0; sc < scTree.getNmbScen(); sc ++) {
		// Get the solution for scenario sc sorted into the original (FlopC++) order:
		int nmbColsInScenario;
		double *ptScenarioSolution = stochModel.getColSolution(sc,&nmbColsInScenario);

		// Get the leaf node of scenario sc:
		SmiScnNode *ptNode = stochModel.getLeafNode(sc);
		int nodeStage = nmbStages;

		double scProb = ptNode->getModelProb(); // probability of the leaf
		double scenObjVal = stochModel.getObjectiveValue(sc); // objective value
		objValue += scProb * scenObjVal;
		printf ("scen %d: prob = %.3f  obj =%7.2f", sc+1, scProb, scenObjVal);

		// This loop traverses the tree, from the leaf to the root
		while (ptNode != NULL) {
			// get number of columns in Node
			int nmbColsInNode = ptNode->getNumCols();
			// get the wealth from the scenario solution
			nodeWealth[nodeStage-1]
				= coreNodes[nodeStage-1]->get_wealth(ptScenarioSolution, nmbColsInNode);
			// Get the parent node (Root will return NULL, stopping the loop)
			ptNode = ptNode->getParent();
			nodeStage--;
		}

		free(ptScenarioSolution);
		printf (";  wealth:");
		for (int t = 0; t<nmbStages-1; t++)
			printf ("%6.2f ->", nodeWealth[t]);
		printf ("%6.2f\n", nodeWealth[nmbStages-1]);
	}

	printf ("%15s Total obj = %7.3f\n", "", objValue);
	assert (fabs(objValue - ptDetEqModel->getObjValue()) < 1e-6 && "Check obj.");

	return 0;
}
