#include "SmiScnData.hpp"
#include "CoinHelperFunctions.hpp"
#include "OsiSolverInterface.hpp"
#include "CoinPackedMatrix.hpp"
#include <iostream>
#include <vector>

using namespace std;

#if 0
int nrow_;
int ncol_;
int nz_; // We can count the total number of elements, but I do not if we need this at one point
SmiStageIndex nstag_; //Total number of stages in the problem, apart from the first stage. A two-stage problem has nstag_ = 1 and so on. If the problem is deterministic, nstag_ = 0.
int *nColInStage_; //Number of Columns in Stage
int *nRowInStage_; //Number of Rows in Stage
int *stageColPtr_; //Start Index of Columns in Stage, with respect to ncol_
int *stageRowPtr_; //Start Index of Rows in Stage, with respect to nrow_
int *colStage_; //To which stage belongs the column?
int *rowStage_; //To which stage belongs the row?
int *colEx2In_; //Not clear yet
int *rowEx2In_; //Not clear yet
int *colIn2Ex_; // Not clear yet
int *rowIn2Ex_; // Not clear yet
int *integerIndices_; //indices of integer variables
int integerLength_; //number of integer variables
int *binaryIndices_; //indices of binary variables
int binaryLength_; //number of binary variables
double **cdrlo_;
double **cdrup_;
double **cdobj_;
double **cdclo_;
double **cdcup_;
vector<SmiNodeData*> nodes_; //Nodes, that contain stage dependent constraints (with Bounds,Ranges,Objective,Matrix), so called CoreNodes 
vector<double *> pDenseRow_; //dense probability vector
vector< vector<int> > intColsStagewise; // For each stage separately, it contains the position of every integer column
#endif

SmiCoreData::SmiCoreData(OsiSolverInterface *osi,int nstag,int *cstag,int *rstag, int* integerIndices,int integerLength, int* binaryIndices, int binaryLength):
nrow_(0),ncol_(0),nz_(0),nstag_(nstag),nColInStage_(NULL),nRowInStage_(NULL),stageColPtr_(NULL),colStage_(NULL),rowStage_(NULL),colEx2In_(NULL),rowEx2In_(NULL),
colIn2Ex_(NULL),rowIn2Ex_(NULL),integerIndices_(NULL),integerLength_(0),binaryIndices_(NULL),binaryLength_(0),
cdrlo_(NULL),cdrup_(NULL),cdobj_(NULL),cdclo_(NULL),cdcup_(NULL),nodes_(),pDenseRow_(),intColsStagewise(nstag,std::vector<int>()),colNamesStrict(NULL),colNamesFree(NULL)
{	//Copies all values already stored in the Solver..
    int nrow = osi->getNumRows();
    int ncol = osi->getNumCols();
    CoinPackedVector *drlo = new CoinPackedVector(nrow,osi->getRowLower());
    CoinPackedVector *drup = new CoinPackedVector(nrow,osi->getRowUpper());
    CoinPackedVector *dclo = new CoinPackedVector(ncol,osi->getColLower());
    CoinPackedVector *dcup = new CoinPackedVector(ncol,osi->getColUpper());
    CoinPackedVector *dobj = new CoinPackedVector(ncol,osi->getObjCoefficients());

    CoinPackedMatrix *matrix = new CoinPackedMatrix(*osi->getMatrixByRow());
    matrix->eliminateDuplicates(0.0);

    gutsOfConstructor(nrow,ncol,nstag,cstag,rstag,matrix,dclo,dcup,dobj,drlo,drup,integerIndices,integerLength,binaryIndices,binaryLength);
    infinity_ = osi->getInfinity();

	setHasQdata(false);

    delete matrix;
    delete drlo;
    delete drup;
    delete dclo;
    delete dcup;
    delete dobj;
}

SmiCoreData::SmiCoreData(CoinMpsIO *osi,int nstag,int *cstag,int *rstag,int* integerIndices,int integerLength, int* binaryIndices, int binaryLength):
nrow_(0),ncol_(0),nz_(0),nstag_(nstag),nColInStage_(NULL),nRowInStage_(NULL),stageColPtr_(NULL),colStage_(NULL),rowStage_(NULL),colEx2In_(NULL),rowEx2In_(NULL),
colIn2Ex_(NULL),rowIn2Ex_(NULL),integerIndices_(NULL),integerLength_(0),binaryIndices_(NULL),binaryLength_(0),
cdrlo_(NULL),cdrup_(NULL),cdobj_(NULL),cdclo_(NULL),cdcup_(NULL),nodes_(),pDenseRow_(),intColsStagewise(nstag,std::vector<int>()),colNamesStrict(NULL),colNamesFree(NULL)
{
    int nrow = osi->getNumRows();
    int ncol = osi->getNumCols();
    CoinPackedVector *drlo = new CoinPackedVector(nrow,osi->getRowLower());
    CoinPackedVector *drup = new CoinPackedVector(nrow,osi->getRowUpper());
    CoinPackedVector *dclo = new CoinPackedVector(ncol,osi->getColLower());
    CoinPackedVector *dcup = new CoinPackedVector(ncol,osi->getColUpper());
    CoinPackedVector *dobj = new CoinPackedVector(ncol,osi->getObjCoefficients());

    CoinPackedMatrix *matrix = new CoinPackedMatrix(*osi->getMatrixByRow());
    matrix->eliminateDuplicates(0.0);
    
    gutsOfConstructor(nrow,ncol,nstag,cstag,rstag,matrix,dclo,dcup,dobj,drlo,drup,integerIndices,integerLength,binaryIndices,binaryLength);
    infinity_ = osi->getInfinity();

	setHasQdata(false);

    delete matrix;
    delete drlo;
    delete drup;
    delete dclo;
    delete dcup;
    delete dobj;
}

OsiSolverInterface* SmiCoreData::generateCoreProblem(OsiSolverInterface* osi){
    osi->reset();
    //Set Matrix

    //Set column, row, objective function

    return osi;
}

void SmiCoreData::gutsOfConstructor( int nrow,int ncol,int nstag, int *cstag,int *rstag, CoinPackedMatrix *matrix, CoinPackedVector *dclo, CoinPackedVector *dcup, CoinPackedVector *dobj, CoinPackedVector *drlo, CoinPackedVector *drup, int* integerIndices /*= 0*/,int integerLength /*= 0*/,int* binaryIndices /*= 0*/,int binaryLength /*= 0*/ )
{
	int i;
	nrow_ = nrow;
	ncol_ = ncol;
    integerIndices_ = new int[integerLength];
    integerLength_ = integerLength;
    memcpy(integerIndices_,integerIndices,sizeof(int)*integerLength);
    binaryIndices_ = new int[binaryLength];
    binaryLength_ = binaryLength;
    memcpy(binaryIndices_,binaryIndices,sizeof(int)*binaryLength);



	// store number stages
	nstag_ = nstag;

	nColInStage_ = new int[nstag_+1];
	nRowInStage_ = new int[nstag_+1];


	colStage_ = new int[ncol_];
	colEx2In_ = new int[ncol_];
	colIn2Ex_ = new int[ncol_];

	rowStage_ = new int[nrow_];
	rowEx2In_ = new int[nrow_];
	rowIn2Ex_ = new int[nrow_];

	// store stage maps
	CoinDisjointCopyN(cstag,ncol_,colStage_);
	CoinDisjointCopyN(rstag,nrow_,rowStage_);

	// zero stage counts
	for (i=0;i<nstag_+1;i++)
	{
		nColInStage_[i] = 0;
		nRowInStage_[i] = 0;
	}

	// array to point to start of new stage
	stageRowPtr_ = new int[nstag_+1];

	// count rows in each stage
	for (i=0;i<nrow_;i++)
		nRowInStage_[rowStage_[i]]++;

	// set stage pointers
	stageRowPtr_[0] = 0;
	for (i=0;i<nstag_;i++)
		stageRowPtr_[i+1] = stageRowPtr_[i] + nRowInStage_[i];

	// place index into next open position in its stage
	for (i=0;i<nrow_;i++)
	{
		rowEx2In_[i] = stageRowPtr_[rowStage_[i]]; //The result of RowEx2In for Row i is the stageRowPointer for the given stage (but because this pointer is increased, nothing happens..)
		rowIn2Ex_[rowEx2In_[i]] = i;
		stageRowPtr_[rowStage_[i]]++;
	}
	cout << endl;

	// reset stage pointers
	stageRowPtr_[0] = 0;
	for (i=0;i<nstag_;i++)
		stageRowPtr_[i+1] = stageRowPtr_[i] + nRowInStage_[i];


	// array to point to start of new stage
	stageColPtr_ = new int[nstag_+1];

	// count cols in each stage
	for (i=0;i<ncol_;i++)
		nColInStage_[colStage_[i]]++;

	// set stage pointers
	stageColPtr_[0] = 0;
	for (i=0;i<nstag_;i++)
		stageColPtr_[i+1] = stageColPtr_[i] + nColInStage_[i];

	// place index into next open position in its stage
	for (i=0;i<ncol_;i++)
	{
		colEx2In_[i] = stageColPtr_[colStage_[i]];
		colIn2Ex_[colEx2In_[i]] = i;
		stageColPtr_[colStage_[i]]++;
	}

    //for (i=0;i<ncol_;i++){
	//	printf("colEx2In = %d, colIn2Ex = %d for index %d\n",colEx2In_[i],colIn2Ex_[i],i);
	//}

	// reset stage pointers
	stageColPtr_[0] = 0;
	for (i=0;i<nstag_;i++)
		stageColPtr_[i+1] = stageColPtr_[i] + nColInStage_[i];

    // store list with values of integer columns for cur stage only
    int * nColInPrevStages = new int[nstag_];
    nColInPrevStages[0] = 0;
    for (i=1; i<nstag_; i++) {
        nColInPrevStages[i] = nColInPrevStages[i-1] + nColInStage_[i-1];
    }    
    for (i=0; i<integerLength_; i++) {
        int indice = integerIndices[i];
        int stage = colStage_[indice];
        intColsStagewise[stage].push_back(indice - nColInPrevStages[stage]);
    }
    delete[] nColInPrevStages;


	// reserve place for nodes holding stage dependent fixed information
	this->nodes_.reserve(nstag_);

	// TODO: specialize this interface for core nodes
	cdclo_ = new double*[nstag_];
	cdcup_ = new double*[nstag_];
	cdobj_ = new double*[nstag_];
	cdrlo_ = new double*[nstag_];
	cdrup_ = new double*[nstag_];
	//Christian: Create core nodes for every stage
	for (i=0;i<nstag_;i++)
	{
		SmiNodeData *node = new SmiNodeData(i,this,
			matrix,dclo,dcup,dobj,drlo,drup);
		node->setCoreNode();
		nodes_.push_back(node);
		int nrow=this->getNumRows(i);
		int ncol=this->getNumCols(i);
		int irow=this->getRowStart(i);
		int icol=this->getColStart(i);
		//Christian: TODO Why is irow added to these vectors? And Why are these vectors created, if no one uses the getMethods?
		CoinPackedVector cpv_rlo(node->getRowLowerLength(),node->getRowLowerIndices(),node->getRowLowerElements());
		cdrlo_[i]=cpv_rlo.denseVector(nrow+irow)+irow;

		CoinPackedVector cpv_rup(node->getRowUpperLength(),node->getRowUpperIndices(),node->getRowUpperElements());
		cdrup_[i]=cpv_rup.denseVector(nrow+irow)+irow;

		CoinPackedVector cpv_clo(node->getColLowerLength(),node->getColLowerIndices(),node->getColLowerElements());
		cdclo_[i]=cpv_clo.denseVector(ncol+icol)+icol;

		CoinPackedVector cpv_cup(node->getColUpperLength(),node->getColUpperIndices(),node->getColUpperElements());
		cdcup_[i]=cpv_cup.denseVector(ncol+icol)+icol;

		CoinPackedVector cpv_obj(node->getObjectiveLength(),node->getObjectiveIndices(),node->getObjectiveElements());
		cdobj_[i]=cpv_obj.denseVector(ncol+icol)+icol;

		// sort indices in each row
		//Christian: TODO Why do we have to sort indices?
		for (int ii=irow; ii<irow+nrow; ++ii)
		{
			if (node->getRowLength(ii)>0)
			{
			CoinPackedVector cpv(node->getRowLength(ii),node->getRowIndices(ii),node->getRowElements(ii));
			cpv.sortIncrIndex();
			memcpy(node->getMutableRowElements(ii),cpv.getElements(),sizeof(double)*cpv.getNumElements());
			memcpy(node->getMutableRowIndices(ii),cpv.getIndices(),sizeof(int)*cpv.getNumElements());
			}

		}
	}

	// reserve space for dense row pointers
	pDenseRow_.resize(nrow_);
	for (int i=0; i<nrow_; ++i) pDenseRow_[i]=NULL;

}

void SmiCoreData::addQuadraticObjectiveToCore(int *starts,int *indx,double *dels){

	int ncols = this->getNumCols();

	sqp_ = new SmiQuadraticData(ncols,starts,indx,dels,0);	//zero offset in Core model
															//note - this is a shallow copy

	if (!sqp_->hasData())
	{
		cout<<"[SmiCoreData::addQuadraticObjectiveToCore] Warning: no quadratic data found.\n";
		return;
	}

	//at this point we have quadratic data
	this->setHasQdata(true);

	for (int t=0;t<this->nstag_;++t)
	{
		nodes_[t]->addQuadraticObjective(t,this,sqp_);
	}
}

void SmiCoreData::copyRowLower(double * d,SmiStageIndex t )
{
	CoinDisjointCopyN(cdrlo_[t],this->getNumRows(t),d);
}

void SmiCoreData::copyRowUpper(double * d,SmiStageIndex t)
{
	CoinDisjointCopyN(cdrup_[t],this->getNumRows(t),d);
}
void SmiCoreData::copyColLower(double * d,SmiStageIndex t)
{
	CoinDisjointCopyN(cdclo_[t],this->getNumCols(t),d);
}
void SmiCoreData::copyColUpper(double * d,SmiStageIndex t)
{
	CoinDisjointCopyN(cdcup_[t],this->getNumCols(t),d);
}
void SmiCoreData::copyObjective(double * d,SmiStageIndex t)
{
	CoinDisjointCopyN(cdobj_[t],this->getNumCols(t),d);
}

SmiCoreData::~SmiCoreData()
{
	for(int t=0; t<this->getNumStages(); ++t)
	{
		int irow=this->getRowStart(t);
		int icol=this->getColStart(t);
		cdrlo_[t]-=irow;
		cdrup_[t]-=irow;
		cdclo_[t]-=icol;
		cdcup_[t]-=icol;
		cdobj_[t]-=icol;
		delete [] cdrlo_[t];
		delete [] cdrup_[t];
		delete [] cdclo_[t];
		delete [] cdcup_[t];
		delete [] cdobj_[t];
	}
	// Now we can delete pointers
	delete [] nColInStage_;
	delete [] nRowInStage_;
	delete [] colStage_;
	delete [] rowStage_;
	delete [] colEx2In_;
	delete [] rowEx2In_;
	delete [] colIn2Ex_;
	delete [] rowIn2Ex_;
	delete [] stageColPtr_;
	delete [] stageRowPtr_;
	delete [] cdrlo_;
	delete [] cdrup_;
	delete [] cdclo_;
	delete [] cdcup_;
	delete [] cdobj_;
    delete [] integerIndices_;
    for (size_t ijk=0; ijk<nodes_.size(); ijk++){
	  delete nodes_[ijk];
    }

    if (colNamesStrict != NULL && colNamesFree != NULL) {
        for (int i = 0; i < this->getNumCols(); i++) {
            delete [] colNamesStrict[i];
            delete [] colNamesFree[i];
        }
        delete [] colNamesStrict;
        delete [] colNamesFree;
    }
}

void
SmiNodeData::setCoreNode()
{
	isCoreNode_=true;
}

// constructor from LP data
// copies present values in given data structures for an SmiNodeData
// TODO: allow for special node data like integer variables not in core, etc
//Christian: Stores information corresponding to given stage for ranges, objective and bounds, but not for the matrix. This needs to be done in a better way, eventually.
//Excess memory is released at the end of the method.
SmiNodeData::SmiNodeData(SmiStageIndex stg, SmiCoreData *core,
				 const CoinPackedMatrix * const matrix,
				 CoinPackedVector *dclo,
				 CoinPackedVector *dcup,
				 CoinPackedVector *dobj,
				 CoinPackedVector *drlo,
				 CoinPackedVector *drup):
				 stg_(stg),
				 core_(core), isCoreNode_(false),
				 numarrays_(5), // 5 arrays: dclo, dcup, dobj, drlo, drup
				 nrow_(core->getNumRows(stg_)),
				 ncol_(core->getNumCols(stg_)),
				 rowbeg_(core->getRowStart(stg_)),
				 colbeg_(core->getColStart(stg_)),
				 ptr_count(0)  // used for counted pointer
{
	//so far no QP data
	this->setHasQdata(false);
	this->nqdata_=NULL;

	// count an upper bound for number elements
	nels_ = 0;
	if (matrix)
		nels_ += matrix->getNumElements();
	if (dclo)
		nels_ += dclo->getNumElements();
	if (dcup)
		nels_ += dcup->getNumElements();
	if (dobj)
		nels_ += dobj->getNumElements();
	if (drlo)
		nels_ += drlo->getNumElements();
	if (drup)
		nels_ += drup->getNumElements();

	// assign memory
	this->assignMemory();

	// temp variables for copying
	int offset_dst=0;
	int offset_src=0;
	int len=0;
	int		*ind=NULL;
	double	*els=NULL;
	int i_start=0;

	// offset_dst always points to the next free spot in SmiNodeData storage
	offset_dst      = 0;

	// Matrix
	//Christian: Copy Matrix Elements. 
	//What is done here? CoinPackedMatrix needs to have the same size as the core Matrix, but only the current stage constraints are read in
	//TODO: Change this behaviour, so that only a small matrix is given to this method?!?!
	this->mat_strt_ = i_start;
	if (matrix && matrix->getNumElements() > 0)
	{
		has_matrix_=true;

		// we need a row-ordered matrix here,
		// so we might need a reversed-ordered copy of 'matrix'.
		CoinPackedMatrix * revOrdMatrix = NULL;
		if (matrix->isColOrdered())
		{
			revOrdMatrix = new CoinPackedMatrix();
			revOrdMatrix->reverseOrderedCopyOf(*matrix);
		}
		// This construction is needed to honour the constness of 'matrix'
		const CoinPackedMatrix * localMatrix
			= (matrix->isColOrdered() ? revOrdMatrix : matrix);

		const double *matrix_els = localMatrix->getElements();
		const int    *matrix_ind = localMatrix->getIndices();
		const int    *matrix_len = localMatrix->getVectorLengths();
		const int    *matrix_str = localMatrix->getVectorStarts();


		// check all rows in the stage of this node
		for (int i=0; i<this->nrow_; ++i)
		{
			// "External" index of a matrix row that belongs to the stage
			int isrc = core->getRowExternalIndex(this->rowbeg_+i);

			//copy the matrix row to destination
			if ((len = matrix_len[isrc]))
			{
				//matrix row start
				offset_src = matrix_str[isrc];

				//copy elements and indices
				memcpy(this->dels_+offset_dst,matrix_els+offset_src,len*sizeof(double));
				memcpy(this->inds_+offset_dst,matrix_ind+offset_src,len*sizeof(int));

				//update offset
				offset_dst += len;
			}
			//store row start for next row
			i_start++;
			this->strt_[i_start] = offset_dst;
		

		}
		//convert indices to "Internal"
		for (int j=0; j<offset_dst; j++)
			this->inds_[j] = core->getColInternalIndex(this->inds_[j]);

		// if we had to make a reversed-ordered copy then delete it now
		if (revOrdMatrix)
		{
			delete revOrdMatrix;
		}
	}
	else
		has_matrix_=false;

	// Column Lower Bound
	this->clo_strt_ = i_start;
	if (dclo)
	{
		ind = dclo->getIndices();
		els = dclo->getElements();
		for (int j=0; j<dclo->getNumElements(); j++)
		{
			int icol = ind[j];
			if ( core->getColStage(icol) == stg )
			{
				this->dels_[offset_dst] = els[j];
				this->inds_[offset_dst] = core->getColInternalIndex(icol);
				offset_dst++;
			}
		}
	}
	i_start++;
	this->strt_[i_start] = offset_dst;

	// Column Upper Bound
	this->cup_strt_ = i_start;
	if (dcup)
	{
		ind = dcup->getIndices();
		els = dcup->getElements();
		for (int j=0; j<dcup->getNumElements(); j++)
		{
			int icol = ind[j];
			if ( core->getColStage(icol) == stg )
			{
				this->dels_[offset_dst] = els[j];
				this->inds_[offset_dst] = core->getColInternalIndex(icol);
				offset_dst++;
			}
		}
	}
	i_start++;
	this->strt_[i_start] = offset_dst;

	// Objective
	this->obj_strt_ = i_start;
	if (dobj)
	{
		ind = dobj->getIndices();
		els = dobj->getElements();
		for (int j=0; j<dobj->getNumElements(); j++)
		{
			int icol = ind[j];
			if ( core->getColStage(icol) == stg )
			{
				this->dels_[offset_dst] = els[j];
				this->inds_[offset_dst] = core->getColInternalIndex(icol);
				offset_dst++;
			}
		}
	}
	i_start++;
	this->strt_[i_start] = offset_dst;

	// Row Lower Bound
	this->rlo_strt_ = i_start;
	if (drlo)
	{
		ind = drlo->getIndices();
		els = drlo->getElements();
		for (int j=0; j<drlo->getNumElements(); j++)
		{
			int irow = ind[j];
			if ( core->getRowStage(irow) == stg )
			{
				this->dels_[offset_dst] = els[j];
				this->inds_[offset_dst] = core->getRowInternalIndex(irow);
				offset_dst++;
			}
		}
	}
	i_start++;
	this->strt_[i_start] = offset_dst;

	// Row Upper Bound
	this->rup_strt_ = i_start;
	if (drup)
	{
		ind = drup->getIndices();
		els = drup->getElements();
		for (int j=0; j<drup->getNumElements(); j++)
		{
			int irow = ind[j];
			if ( core->getRowStage(irow) == stg )
			{
				this->dels_[offset_dst] = els[j];
				this->inds_[offset_dst] = core->getRowInternalIndex(irow);
				offset_dst++;
			}
		}
	}
	i_start++;
	this->strt_[i_start] = offset_dst;

	// return excess memory to the heap
    void *temp_ptr = realloc(this->dels_,offset_dst*sizeof(double));
    if (temp_ptr) 
        this->dels_ = (double*)temp_ptr;
    temp_ptr = realloc(this->inds_,offset_dst*sizeof(int));
    if (temp_ptr)
        this->inds_ = (int*)temp_ptr;
}

void SmiNodeData::addQuadraticObjective(int stg, SmiCoreData *smicore, SmiQuadraticData *sqdata)
{
	// should only get here if there is quadratic data
	assert(sqdata->hasData());

	// only core nodes have QP data.
	assert(this->isCoreNode());

	// add a deep copy of node's QP data to node object
	// test to make sure that there are no node-node interactions in the Q data
	
	// Quadratic Objective
	{
		int * strts		= sqdata->getQDstarts();
		int * ind		= sqdata->getQDindx();
		double * els	= sqdata->getQDels();
		int nels		= sqdata->getNumEls();
		int ncols		= smicore->getNumCols();

		nqdata_ = new SmiQuadraticDataDC(ncols,nels);			//node has deep copy of Qdata

		int *nqstarts = nqdata_->getQDstarts();

		//record where node Q data columns are in the new column ordering
		int iels,icol;
		nqstarts[0]=0;
		for (int j=0; j<ncols; ++j)
		{
			if (smicore->getColStage(j) == stg)		
			{
				icol = smicore->getColInternalIndex(j);
				assert(icol<ncols);
				iels = strts[j+1] - strts[j];
				nqstarts[icol+1]=iels;
			}
		}
		//set column starts in the new new column ordering
		for (int j=0;j<ncols; ++j)
		{
			nqstarts[j+1]+=nqstarts[j];
		}

		int nqels = nqstarts[ncols];

		//return if no Qdata for this node
		if (nqels == 0)
		{
			this->setHasQdata(false);
			delete nqdata_;
			return;
		}


		this->setHasQdata(true);
		nqdata_->setHasData(true);

		int *nqindx		= nqdata_->getQDindx();
		double *nqdels	= nqdata_->getQDels();

		//copy data
		for (int j=0; j<ncols; ++j)
		{
			if (smicore->getColStage(j)==stg)
			{
													// get column id for new ordering
				icol = smicore->getColInternalIndex(j);
													// reset pointer to new entry
				int ilocal=0;
		
				for (int jj = strts[j]; jj<strts[j+1];++jj)
				{
					if (smicore->getColStage(ind[jj]) != stg)
													// throw exception if column index is not in the node's stage
					{
					   string s="Exception: Quadratic data for timestage "+stg;
					   s+=" includes data from timestage "+smicore->getColStage(ind[jj]);
					   throw s;
					   //std::exception e();
					   //throw(e);
					}
													// find location for new entry
					int ii = nqstarts[icol]+ilocal;
													// enter index and element information
					nqindx[ii]  = smicore->getColInternalIndex(ind[jj]);
					nqdels[ii]  = els[jj];
													// update pointer to new entry
					++ilocal;


				}
				//sanity check
				assert(ilocal == nqstarts[icol+1] - nqstarts[icol]);
			}
		}
	}
}

int SmiNodeData::combineWithDenseCoreRow(double *dr,const int nels,const int *inds, const double *dels, double *dest_dels,int *dest_indx)
{
	return this->getCoreCombineRule()->Process(dr,this->getCore()->getNumCols(), nels,inds,dels,dest_dels,dest_indx);
}
int SmiNodeData::combineWithDenseCoreRow(double *dr,CoinPackedVector *cpv,double *dels,int *indx)
{
	return getCoreCombineRule()->Process(dr,this->getCore()->getNumCols(),cpv,dels,indx);
}

CoinPackedVector * SmiNodeData::combineWithCoreRow(CoinPackedVector *cr, CoinPackedVector *nr)
{
	CoinPackedVector *cpv = getCoreCombineRule()->Process(cr,nr);
	return cpv;
}

void SmiNodeData::combineWithCoreDoubleArray(double *d_out, const CoinPackedVector &cpv, int o)
{
	if (!isCoreNode_)
		getCoreCombineRule()->Process(d_out,o,cpv);
}
void SmiNodeData::combineWithCoreDoubleArray(double *d_out, const int len, const int * inds, const double *dels, int o)
{
	if (!isCoreNode_)
		getCoreCombineRule()->Process(d_out,o,len,inds,dels);
}

void SmiNodeData::copyRowLower(double * d)
{
	int t=getStage();
	getCore()->copyRowLower(d,t);
	combineWithCoreDoubleArray(d,getRowLowerLength(),getRowLowerIndices(),getRowLowerElements(),getCore()->getRowStart(t));
}

void SmiNodeData::copyRowUpper(double * d){
	int t=getStage();
	getCore()->copyRowUpper(d,t);
	combineWithCoreDoubleArray(d,getRowUpperLength(),getRowUpperIndices(),getRowUpperElements(),getCore()->getRowStart(t));
}

void SmiNodeData::copyColLower(double * d){
	int t=getStage();
	getCore()->copyColLower(d,t);
	combineWithCoreDoubleArray(d,getColLowerLength(),getColLowerIndices(),getColLowerElements(),getCore()->getColStart(t));
}

void SmiNodeData::copyColUpper(double * d){
	int t=getStage();
	getCore()->copyColUpper(d,t);
	combineWithCoreDoubleArray(d,getColUpperLength(),getColUpperIndices(),getColUpperElements(),getCore()->getColStart(t));
}

void SmiNodeData::copyObjective(double * d){
	int t=getStage();
	getCore()->copyObjective(d,t);
	combineWithCoreDoubleArray(d,getObjectiveLength(),getObjectiveIndices(),getObjectiveElements(),getCore()->getColStart(t));
}

SmiNodeData::~SmiNodeData()
{
	SmiRowMap::iterator iRowMap;

	SmiDenseRowMap::iterator idRowMap;
	for (idRowMap=dRowMap.begin(); idRowMap!=dRowMap.end(); ++idRowMap)
	  delete[] idRowMap->second;

	deleteMemory();
}

double *
SmiNodeData::getDenseRow(int i) {

		int denseSize=this->getCore()->getNumCols();
		double *dv = dRowMap[i];

		if ( dv == NULL )
		{
			dv = new double[denseSize];  //this is deleted in the SmiNodeData destructor
			dRowMap[i] = dv;
		}
		const int  len = this->getRowLength(i);
		const int *ind = this->getRowIndices(i);
		const double *els = this->getRowElements(i);

		CoinFillN(dv, denseSize, 0.0);  //we have to regenerate this because dv entries can be over-written in SmiCoreCombineRule->Process()
		for (int j = 0; j < len; ++j)
		    dv[ind[j]] = els[j];

		return dRowMap[i];
}


void
SmiNodeData::assignMemory()
{
	
	this->nstrt_    = (this->nrow_+1) + this->numarrays_;
	
	if (this->nels_ == 0)
	{
		this->dels_ = NULL;
		this->inds_ = NULL;
	}
	else
	{		
		this->dels_     = (double *)calloc(this->nels_  ,sizeof(double));
		this->inds_     = (int *)   calloc(this->nels_  ,sizeof(int)   );
	}
	
	this->strt_     = (int *)   calloc(this->nstrt_ ,sizeof(int)   );
}

void
SmiNodeData::deleteMemory()
{
	if (this->dels_)
	{
		free(this->dels_);
		this->dels_=NULL;
	}
	if (this->inds_)
	{
		free(this->inds_);
		this->inds_=NULL;
	}
	if (this->strt_)
	{
		free(this->strt_);
		this->strt_=NULL;
	}
}
