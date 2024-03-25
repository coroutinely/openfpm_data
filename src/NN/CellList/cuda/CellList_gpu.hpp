
#ifndef OPENFPM_DATA_SRC_NN_CELLLIST_CELLLIST_GPU_HPP_
#define OPENFPM_DATA_SRC_NN_CELLLIST_CELLLIST_GPU_HPP_

#include "config.h"

#ifdef CUDA_GPU

#include "Vector/map_vector_sparse.hpp"
#include "NN/CellList/CellDecomposer.hpp"
#include "Vector/map_vector.hpp"
#include "Cuda_cell_list_util_func.hpp"
#include "CellList_gpu_ker.cuh"
#include "util/cuda_util.hpp"
#include "NN/CellList/CellList_util.hpp"
#include "NN/CellList/CellList.hpp"
#include "util/cuda/scan_ofp.cuh"


template<unsigned int dim,
	typename T,
	typename Memory = CudaMemory,
	typename transform_type = no_transform_only<dim,T>,
	bool is_sparse = false>
class CellList_gpu;

template<unsigned int dim, typename T, typename Memory, typename transform_type>
class CellList_gpu<dim,T,Memory,transform_type,false> : public CellDecomposer_sm<dim,T,transform_type>
{
public:
	typedef int ids_type;

private:
	//! \brief Number of particles in each cell
	openfpm::vector_gpu<aggregate<unsigned int>> numPartInCell;

	//! \brief Used to convert cellIndex_LocalIndex to particle id
	openfpm::vector_gpu<aggregate<unsigned int>> cellIndexLocalIndexToPart;

	//! \brief Cell scan with + operation of numPartInCell
	openfpm::vector_gpu<aggregate<unsigned int>> numPartInCellPrefixSum;

	//! \brief particle ids information the first "dim" componets is the cell-id in grid coordinates, the last is the local-id inside the cell
	openfpm::vector_gpu<aggregate<unsigned int[2]>> cellIndex_LocalIndex;

	//! \brief for each sorted index it show the index in the unordered
	openfpm::vector_gpu<aggregate<unsigned int>> sortedToUnsortedIndex;

	//! \brief the index of all the domain particles in the sorted vector
	openfpm::vector_gpu<aggregate<unsigned int>> indexSorted;

	//! \brief for each non sorted index it show the index in the ordered vector
	openfpm::vector_gpu<aggregate<unsigned int>> unsortedToSortedIndex;

	//! \breif Number of neighbors in every direction
	size_t boxNeighborNumber;

	//! \brief Neighborhood cell linear ids (minus middle cell id) for in total (2*boxNeighborNumber+1)**dim cells
	openfpm::vector_gpu<aggregate<int>> boxNeighborCellOffset;

	//! /brief unit cell dimensions, given P1 = (0,0...)
	openfpm::array<T,dim> unitCellP2;

	//! \brief number of sub-divisions in each direction
	openfpm::array<ids_type,dim> numCellDim;

	//! \brief cell padding
	openfpm::array<ids_type,dim> cellPadDim;

	//! \brief Neighboor cell linear ids (minus middle cell id) for \sum_{i=0}^{i=dim}dim[i]/r_cut cells 
	openfpm::vector_gpu<aggregate<int>> rcutNeighborCellOffset;

	//! Additional information in general (used to understand if the cell-list)
	//! has been constructed from an old decomposition
	//! indicate how many times decompose/refine/re-decompose has been called
	size_t nDecRefRedec;

	//! Initialize the structures of the data structure
	void InitializeStructures(
		const size_t (& div)[dim],
		size_t tot_n_cell,
		size_t pad)
	{
		for (size_t i = 0 ; i < dim ; i++)
		{
			numCellDim[i] = div[i];
			unitCellP2[i] = this->getCellBox().getP2().get(i);
			cellPadDim[i] = pad;
		}

		numPartInCell.resize(tot_n_cell);

		boxNeighborNumber = 1;
		constructNeighborCellOffset(boxNeighborNumber);
	}

	void constructNeighborCellOffset(size_t boxNeighborNumber)
	{

		NNcalc_box(boxNeighborNumber,boxNeighborCellOffset,this->getGrid());

		boxNeighborCellOffset.template hostToDevice<0>();
	}

	/*! \brief Construct the ids of the particles domain in the sorted array
	 *
	 * \param gpuContext gpu context
	 *
	 */
	void construct_domain_ids(
		gpu::ofp_context_t& gpuContext,
		size_t start,
		size_t stop,
		size_t ghostMarker)
	{
#ifdef __NVCC__
		//! Sorted domain particles domain or ghost
		openfpm::vector_gpu<aggregate<unsigned int>> isSortedIndexOrGhost;

		isSortedIndexOrGhost.resize(stop-start+1);

		auto ite = isSortedIndexOrGhost.getGPUIterator();

		CUDA_LAUNCH((mark_domain_particles),ite,
			sortedToUnsortedIndex.toKernel(),
			isSortedIndexOrGhost.toKernel(),
			ghostMarker
		);

		openfpm::scan(
			(unsigned int *)isSortedIndexOrGhost.template getDeviceBuffer<0>(),
			isSortedIndexOrGhost.size(),
			(unsigned int *)isSortedIndexOrGhost.template getDeviceBuffer<0>(),
			gpuContext
		);

		isSortedIndexOrGhost.template deviceToHost<0>(isSortedIndexOrGhost.size()-1,isSortedIndexOrGhost.size()-1);
		auto sz = isSortedIndexOrGhost.template get<0>(isSortedIndexOrGhost.size()-1);

		indexSorted.resize(sz);

		CUDA_LAUNCH((collect_domain_ghost_ids),ite,
			isSortedIndexOrGhost.toKernel(),
			indexSorted.toKernel()
		);
#endif
	}

	/*! \brief This function construct a dense cell-list
	 *
	 *
	 */
	template<typename vector, typename vector_prp, unsigned int ... prp>
	void construct_dense(
		vector & vPos,
		vector & vPosOut,
		vector_prp & vPrp,
		vector_prp & vPrpOut,
		gpu::ofp_context_t& gpuContext,
		size_t ghostMarker,
		size_t start,
		size_t stop,
		cl_construct_opt opt = cl_construct_opt::Full)
	{
#ifdef __NVCC__
		// if stop if the default set to the number of particles
		if (stop == (size_t)-1) stop = vPos.size();

		auto ite_gpu = vPos.getGPUIteratorTo(stop-start-1);

		// cellListGrid.size() returns total size of the grid
		numPartInCell.resize(this->cellListGrid.size()+1);
		numPartInCell.template fill<0>(0);

		cellIndex_LocalIndex.resize(stop - start);

		if (ite_gpu.wthr.x == 0 || vPos.size() == 0 || stop == 0)
		{
			// no particles
			numPartInCellPrefixSum.resize(numPartInCell.size());
			numPartInCellPrefixSum.template fill<0>(0);
			return;
		}

		CUDA_LAUNCH((fill_cellIndex_LocalIndex<dim,T,ids_type>),ite_gpu,
			numCellDim,
			unitCellP2,
			cellPadDim,
			this->getTransform(),
			stop,
			start,
			vPos.toKernel(),
			numPartInCell.toKernel(),
			cellIndex_LocalIndex.toKernel()
		);

		// now we scan
		numPartInCellPrefixSum.resize(numPartInCell.size());
		openfpm::scan(
			(unsigned int *)numPartInCell.template getDeviceBuffer<0>(),
			numPartInCell.size(),
			(unsigned int *)numPartInCellPrefixSum.template getDeviceBuffer<0>(),
			gpuContext
		);

		// now we construct the cellIndexLocalIndexToPart

		cellIndexLocalIndexToPart.resize(stop-start);
		auto itgg = cellIndex_LocalIndex.getGPUIterator();


#ifdef MAKE_CELLLIST_DETERMINISTIC

        CUDA_LAUNCH((fill_cells),itgg,
		cellIndex_LocalIndex.size(),
		cellIndexLocalIndexToPart.toKernel()
        );

        // sort
        // gpu::mergesort(
        // 	static_cast<unsigned int *>(cellIndex_LocalIndex.template getDeviceBuffer<0>()),
        // 	static_cast<unsigned int *>(cellIndexLocalIndexToPart.template getDeviceBuffer<0>()),
        // 	vPos.size(),
        // 	gpu::less_t<unsigned int>(),
        // 	gpuContext
        // );
#else

        CUDA_LAUNCH((fill_cells),itgg,
			numPartInCellPrefixSum.toKernel(),
			cellIndex_LocalIndex.toKernel(),
			cellIndexLocalIndexToPart.toKernel(),
			start
		);

#endif
		sortedToUnsortedIndex.resize(stop-start);
		unsortedToSortedIndex.resize(vPos.size());

		auto ite = vPos.getGPUIteratorTo(stop-start,64);

		if (sizeof...(prp) == 0)
		{
			// Here we reorder the particles to improve coalescing access
			CUDA_LAUNCH((reorderParticles),ite,
			   vPrp.toKernel(),
			   vPrpOut.toKernel(),
			   vPos.toKernel(),
			   vPosOut.toKernel(),
			   sortedToUnsortedIndex.toKernel(),
			   unsortedToSortedIndex.toKernel(),
			   cellIndexLocalIndexToPart.toKernel()
			);
		}
		else
		{
			// Here we reorder the particles to improve coalescing access
			CUDA_LAUNCH((reorderParticlesPrp<decltype(vPrp.toKernel()),
					decltype(vPos.toKernel()),
					decltype(sortedToUnsortedIndex.toKernel()),
					decltype(cellIndexLocalIndexToPart.toKernel()),prp...>),ite,
				sortedToUnsortedIndex.size(),
				vPrp.toKernel(),
				vPrpOut.toKernel(),
				vPos.toKernel(),
				vPosOut.toKernel(),
				sortedToUnsortedIndex.toKernel(),
				unsortedToSortedIndex.toKernel(),
				cellIndexLocalIndexToPart.toKernel()
			);
	}

	if (opt == cl_construct_opt::Full)
		construct_domain_ids(gpuContext,start,stop,ghostMarker);

	#else

			std::cout << "Error: " <<  __FILE__ << ":" << __LINE__ << " you are calling CellList_gpu.construct() this function is suppose must be compiled with NVCC compiler, but it look like has been compiled by the standard system compiler" << std::endl;

	#endif
	}

public:

	//! Indicate that this cell list is a gpu type cell-list
	typedef int yes_is_gpu_celllist;

	// typedefs needed for toKernel_transform

	static const unsigned int dim_ = dim;
	typedef T stype_;
	typedef ids_type ids_type_;
	typedef transform_type transform_type_;
	typedef boost::mpl::bool_<false> is_sparse_;

	// end of typedefs needed for toKernel_transform

	/*! \brief Copy constructor
	 *
	 * \param clg Cell list to copy
	 *
	 */
	CellList_gpu(const CellList_gpu<dim,T,Memory,transform_type> & clg)
	{
		this->operator=(clg);
	}

	/*! \brief Copy constructor from temporal
	 *
	 *
	 *
	 */
	CellList_gpu(CellList_gpu<dim,T,Memory,transform_type> && clg)
	{
		this->operator=(clg);
	}

	/*! \brief default constructor
	 *
	 *
	 */
	CellList_gpu()
	{}

	CellList_gpu(
		const Box<dim,T> & box,
		const size_t (&div)[dim],
		const size_t pad = 1)
	{
		Initialize(box,div,pad);
	}

	void setBoxNN(size_t n_NN)
	{
		boxNeighborNumber = n_NN;
		constructNeighborCellOffset(n_NN);
	}

	void resetBoxNN()
	{
		constructNeighborCellOffset(boxNeighborNumber);
	}

	/*! Initialize the cell list constructor
	 *
	 * \param box Domain where this cell list is living
	 * \param div grid size on each dimension
	 * \param pad padding cell
	 * \param slot maximum number of slot
	 *
	 */
	void Initialize(
		const Box<dim,T> & box,
		const size_t (&div)[dim],
		const size_t pad = 1)
	{
		Matrix<dim,T> mat;
		CellDecomposer_sm<dim,T,transform_type>::setDimensions(box,div, mat, pad);

		// create the array that store the number of particle on each cell and se it to 0
		InitializeStructures(this->cellListGrid.getSize(),this->cellListGrid.size(),pad);
	}

	inline openfpm::vector_gpu<aggregate<unsigned int>> &
	getSortToNonSort() {
		return sortedToUnsortedIndex;
	}

	inline openfpm::vector_gpu<aggregate<unsigned int>> &
	getNonSortToSort() {
		return unsortedToSortedIndex;
	}

	inline openfpm::vector_gpu<aggregate<unsigned int>> &
	getDomainSortIds() {
		return indexSorted;
	}


	/*! \brief Set the radius for the getNNIteratorRadius
	 *
	 * \param radius
	 *
	 */
	void setRadius(T radius)
	{
		NNcalc_rad(radius,rcutNeighborCellOffset,this->getCellBox(),this->getGrid());

		rcutNeighborCellOffset.template hostToDevice<0>();
	}

	/*! \brief construct from a list of particles
	 *
	 * \warning vPos is assumed to be already be in device memory
	 *
	 * \param vPos Particles list
	 *
	 */
	template<typename vector, typename vector_prp, unsigned int ... prp>
	void construct(
		vector & vPos,
		vector & vPosOut,
		vector_prp & vPrp,
		vector_prp & vPrpOut,
		gpu::ofp_context_t& gpuContext,
		size_t ghostMarker = 0,
		size_t start = 0,
		size_t stop = (size_t)-1,
		cl_construct_opt opt = cl_construct_opt::Full)
	{
		construct_dense<vector,vector_prp,prp...>(vPos,vPosOut,vPrp,vPrpOut,gpuContext,ghostMarker,start,stop,opt);
	}

	CellList_gpu_ker<dim,T,ids_type,transform_type,false> toKernel()
	{
		const int* p = (int*)boxNeighborCellOffset.toKernel().template getPointer<0>();

		return CellList_gpu_ker<dim,T,ids_type,transform_type,false>(
			numPartInCellPrefixSum.toKernel(),
			sortedToUnsortedIndex.toKernel(),
			indexSorted.toKernel(),
			rcutNeighborCellOffset.toKernel(),
			p,
			boxNeighborCellOffset.size(),
			unitCellP2,
			numCellDim,
			cellPadDim,
			this->getTransform(),
			ghostMarker,
			this->cellListSpaceBox,
			this->cellListGrid,
			this->cellShift
		);
};

	/*! \brief Clear the structure
	 *
	 *
	 */
	void clear()
	{
		numPartInCell.clear();
		cellIndexLocalIndexToPart.clear();
		numPartInCellPrefixSum.clear();
		cellIndex_LocalIndex.clear();
		sortedToUnsortedIndex.clear();
	}

	/////////////////////////////////////

	//! Ghost marker
	size_t ghostMarker = 0;

	/*! \brief return the ghost marker
	 *
	 * \return ghost marker
	 *
	 */
	inline size_t get_gm()
	{
		return ghostMarker;
	}

	/*! \brief Set the ghost marker
	 *
	 * \param ghostMarker marker
	 *
	 */
	inline void set_gm(size_t ghostMarker)
	{
		this->ghostMarker = ghostMarker;
	}

	/////////////////////////////////////

	/*! \brief Set the nDecRefRedec number
	 *
	 * \param nDecRefRedec
	 *
	 */
	void set_ndec(size_t nDecRefRedec)
	{
		this->nDecRefRedec = nDecRefRedec;
	}

	/*! \brief Set the nDecRefRedec number
	 *
	 * \return nDecRefRedec
	 *
	 */
	size_t get_ndec() const
	{
		return nDecRefRedec;
	}

	/////////////////////////////////////

	/*! \brief Transfer the information computed on gpu to construct the cell-list on gpu
	 *
	 */
	void debug_deviceToHost()
	{
		numPartInCell.template deviceToHost<0>();
		cellIndexLocalIndexToPart.template deviceToHost<0>();
		numPartInCellPrefixSum.template deviceToHost<0>();
	}

	/*! \brief Return the numbers of cells contained in this cell-list
	 *
	 * \return the number of cells
	 *
	 */
	size_t getNCells()
	{
		return numPartInCell.size();
	}

	/*! \brief Return the numbers of elements in the cell
	 *
	 * \return the number of elements in the cell
	 *
	 */
	size_t getNelements(size_t i)
	{
		return numPartInCell.template get<0>(i);
	}

	/*! \brief Get an element in the cell
	 *
	 * \tparam i property to get
	 *
	 * \param cell cell id
	 * \param ele element id
	 *
	 * \return The element value
	 *
	 */
	inline auto get(size_t cell, size_t ele) -> decltype(cellIndexLocalIndexToPart.template get<0>(numPartInCellPrefixSum.template get<0>(cell)+ele))
	{
		return cellIndexLocalIndexToPart.template get<0>(numPartInCellPrefixSum.template get<0>(cell)+ele);
	}

	/*! \brief Get an element in the cell
	 *
	 * \tparam i property to get
	 *
	 * \param cell cell id
	 * \param ele element id
	 *
	 * \return The element value
	 *
	 */
	inline auto get(size_t cell, size_t ele) const -> decltype(cellIndexLocalIndexToPart.template get<0>(numPartInCellPrefixSum.template get<0>(cell)+ele))
	{
		return cellIndexLocalIndexToPart.template get<0>(numPartInCellPrefixSum.template get<0>(cell)+ele);
	}

	/*! \brief swap the information of the two cell-lists
	 *
	 *
	 *
	 */
	void swap(CellList_gpu<dim,T,Memory,transform_type,false> & clg)
	{
		((CellDecomposer_sm<dim,T,transform_type> *)this)->swap(clg);
		numPartInCell.swap(clg.numPartInCell);
		cellIndexLocalIndexToPart.swap(clg.cellIndexLocalIndexToPart);
		numPartInCellPrefixSum.swap(clg.numPartInCellPrefixSum);
		cellIndex_LocalIndex.swap(clg.cellIndex_LocalIndex);
		sortedToUnsortedIndex.swap(clg.sortedToUnsortedIndex);
		indexSorted.swap(clg.indexSorted);
		unsortedToSortedIndex.swap(clg.unsortedToSortedIndex);

		unitCellP2.swap(clg.unitCellP2);
		numCellDim.swap(clg.numCellDim);
		cellPadDim.swap(clg.cellPadDim);

		size_t g_m_tmp = ghostMarker;
		ghostMarker = clg.ghostMarker;
		clg.ghostMarker = g_m_tmp;

		size_t n_dec_tmp = nDecRefRedec;
		nDecRefRedec = clg.nDecRefRedec;
		clg.nDecRefRedec = n_dec_tmp;
	}

	CellList_gpu<dim,T,Memory,transform_type,false> &
	operator=(const CellList_gpu<dim,T,Memory,transform_type,false> & clg)
	{
		*static_cast<CellDecomposer_sm<dim,T,transform_type> *>(this) = *static_cast<const CellDecomposer_sm<dim,T,transform_type> *>(&clg);
		numPartInCell = clg.numPartInCell;
		cellIndexLocalIndexToPart = clg.cellIndexLocalIndexToPart;
		numPartInCellPrefixSum = clg.numPartInCellPrefixSum;
		cellIndex_LocalIndex = clg.cellIndex_LocalIndex;
		sortedToUnsortedIndex = clg.sortedToUnsortedIndex;
		indexSorted = clg.indexSorted;
		unsortedToSortedIndex = clg.unsortedToSortedIndex;

		unitCellP2 = clg.unitCellP2;
		numCellDim = clg.numCellDim;
		cellPadDim = clg.cellPadDim;
		ghostMarker = clg.ghostMarker;
		nDecRefRedec = clg.nDecRefRedec;

		return *this;
	}

	CellList_gpu<dim,T,Memory,transform_type> &
	operator=(CellList_gpu<dim,T,Memory,transform_type> && clg)
	{
		static_cast<CellDecomposer_sm<dim,T,transform_type> *>(this)->swap(*static_cast<CellDecomposer_sm<dim,T,transform_type> *>(&clg));
		numPartInCell.swap(clg.numPartInCell);
		cellIndexLocalIndexToPart.swap(clg.cellIndexLocalIndexToPart);
		numPartInCellPrefixSum.swap(clg.numPartInCellPrefixSum);
		cellIndex_LocalIndex.swap(clg.cellIndex_LocalIndex);
		sortedToUnsortedIndex.swap(clg.sortedToUnsortedIndex);
		indexSorted.swap(clg.indexSorted);
		unsortedToSortedIndex.swap(clg.unsortedToSortedIndex);

		unitCellP2 = clg.unitCellP2;
		numCellDim = clg.numCellDim;
		cellPadDim = clg.cellPadDim;
		ghostMarker = clg.ghostMarker;
		nDecRefRedec = clg.nDecRefRedec;

		return *this;
	}
};


template<unsigned int dim, typename T, typename Memory, typename transform_type>
class CellList_gpu<dim,T,Memory,transform_type,true> : public CellDecomposer_sm<dim,T,transform_type>
{
public:
	typedef int ids_type;

private:
	//! \brief for each cell the particles id in it
	openfpm::vector_gpu<aggregate<unsigned int>> cellIndexLocalIndexToPart;

	//! \brief contains particle cell indexes
	openfpm::vector_gpu<aggregate<unsigned int>> cellIndex;

	//! \brief sparse vector to segreduce the
	openfpm::vector_sparse_gpu<aggregate<unsigned int>> vecSparseCellIndex_PartIndex;

	//! \brief number of neighborhood each cell cell has + offset
	openfpm::vector_gpu<aggregate<unsigned int>> neighborCellCount;

	//! \brief For each cell the list of the neighborhood cells
	openfpm::vector_gpu<aggregate<unsigned int,unsigned int>> neighborPartIndexFrom_To;

	//! \breif Number of neighbors in every direction
	size_t boxNeighborNumber;

	//! \brief Neighborhood cell linear ids (minus middle cell id) for in total (2*boxNeighborNumber+1)**dim cells
	openfpm::vector_gpu<aggregate<int>> boxNeighborCellOffset;

	//! \brief for each sorted index it show the index in the unordered
	openfpm::vector_gpu<aggregate<unsigned int>> sortedToUnsortedIndex;

	//! \brief the index of all the domain particles in the sorted vector
	openfpm::vector_gpu<aggregate<unsigned int>> indexSorted;

	//! \brief for each non sorted index it show the index in the ordered vector
	openfpm::vector_gpu<aggregate<unsigned int>> unsortedToSortedIndex;

	//! /brief unit cell dimensions
	openfpm::array<T,dim> unitCellP2;

	//! \brief number of sub-divisions in each direction
	openfpm::array<ids_type,dim> numCellDim;

	//! \brief cell padding
	openfpm::array<ids_type,dim> cellPadDim;

	//! Additional information in general (used to understand if the cell-list)
	//! has been constructed from an old decomposition
	size_t nDecRefRedec;

	//! Initialize the structures of the data structure
	void InitializeStructures(
		const size_t (& div)[dim],
		size_t tot_n_cell,
		size_t pad)
	{
		for (size_t i = 0 ; i < dim ; i++)
		{
			numCellDim[i] = div[i];
			unitCellP2[i] = this->getCellBox().getP2().get(i);
			cellPadDim[i] = pad;
		}

		boxNeighborNumber = 1;
		constructNeighborCellOffset(boxNeighborNumber);
	}

	void constructNeighborCellOffset(size_t boxNeighborNumber)
	{
		NNcalc_box(boxNeighborNumber,boxNeighborCellOffset,this->getGrid());

		boxNeighborCellOffset.template hostToDevice<0>();
	}

	/*! \brief This function construct a sparse cell-list
	 *
	 *
	 */
	template<typename vector, typename vector_prp, unsigned int ... prp>
	void construct_sparse(
		vector & vPos,
		vector & vPosOut,
		vector_prp & vPrp,
		vector_prp & vPrpOut,
		gpu::ofp_context_t& gpuContext,
		size_t ghostMarker,
		size_t start,
		size_t stop,
		cl_construct_opt opt = cl_construct_opt::Full)
	{
#ifdef __NVCC__
		// if stop if the default set to the number of particles
		if (stop == (size_t)-1) stop = vPos.size();

		cellIndex.resize(stop - start);
		cellIndex.template fill<0>(0);

		auto ite_gpu = vPos.getGPUIteratorTo(stop-start);

		if (ite_gpu.wthr.x == 0 || vPos.size() == 0 || stop == 0)
			return;

		CUDA_LAUNCH((fill_cellIndex<dim,T,ids_type>),ite_gpu,
			numCellDim,
			unitCellP2,
			cellPadDim,
			this->getTransform(),
			vPos.size(),
			start,
			vPos.toKernel(),
			cellIndex.toKernel()
		);

		cellIndexLocalIndexToPart.resize(stop-start);

		// Here we fill the sparse vector
		vecSparseCellIndex_PartIndex.clear();
		vecSparseCellIndex_PartIndex.template setBackground<0>((unsigned int)-1);
		vecSparseCellIndex_PartIndex.setGPUInsertBuffer(ite_gpu.wthr.x,ite_gpu.thr.x);

		CUDA_LAUNCH((fill_vsCellIndex_PartIndex),ite_gpu,
			vecSparseCellIndex_PartIndex.toKernel(),
			cellIndex.toKernel()
		);

		// flush_vd<sstart_<0>> returns the comulative prefix for cell indexes
		vecSparseCellIndex_PartIndex.template flush_vd<sstart_<0>>(cellIndexLocalIndexToPart,gpuContext,FLUSH_ON_DEVICE);

		neighborCellCount.resize(vecSparseCellIndex_PartIndex.size()+1);
		neighborCellCount.template fill<0>(0);

		// for every particle increase the counter for every non-zero neighbor cell
		auto itgg = vecSparseCellIndex_PartIndex.getGPUIterator();
		CUDA_LAUNCH((countNeighborCells),itgg,
			vecSparseCellIndex_PartIndex.toKernel(),
			neighborCellCount.toKernel(),
			boxNeighborCellOffset.toKernel()
		);

		// get total number of non-empty neighboring cells
		openfpm::scan(
			(unsigned int *)neighborCellCount.template getDeviceBuffer<0>(),
			neighborCellCount.size(),
			(unsigned int *)neighborCellCount.template getDeviceBuffer<0>(),
			gpuContext
		);

		neighborCellCount.template deviceToHost<0>(neighborCellCount.size() - 1, neighborCellCount.size() - 1);
		size_t totalNeighborCellCount = neighborCellCount.template get<0>(neighborCellCount.size()-1);

		neighborPartIndexFrom_To.resize(totalNeighborCellCount);
		CUDA_LAUNCH((fillNeighborCellList),itgg,
			vecSparseCellIndex_PartIndex.toKernel(),
			neighborCellCount.toKernel(),
			boxNeighborCellOffset.toKernel(),
			neighborPartIndexFrom_To.toKernel(),
			cellIndexLocalIndexToPart.size()
		);

		sortedToUnsortedIndex.resize(stop-start);
		unsortedToSortedIndex.resize(vPos.size());

		auto ite = vPos.getGPUIteratorTo(stop-start,64);

		// Here we reorder the particles to improve coalescing access
		CUDA_LAUNCH((reorderParticles),ite,
			vPrp.toKernel(),
			vPrpOut.toKernel(),
			vPos.toKernel(),
			vPosOut.toKernel(),
			sortedToUnsortedIndex.toKernel(),
			unsortedToSortedIndex.toKernel(),
			cellIndexLocalIndexToPart.toKernel()
		);

		if (opt == cl_construct_opt::Full)
			construct_domain_ids(gpuContext,start,stop,ghostMarker);
	#else
			std::cout << "Error: " <<  __FILE__ << ":" << __LINE__ << " you are calling CellList_gpu.construct() this function is suppose must be compiled with NVCC compiler, but it look like has been compiled by the standard system compiler" << std::endl;
	#endif
	}

	/*! \brief Construct the ids of the particles domain in the sorted array
	 *
	 * \param gpuContext gpu context
	 *
	 */
	void construct_domain_ids(
		gpu::ofp_context_t& gpuContext,
		size_t start,
		size_t stop,
		size_t ghostMarker)
	{
#ifdef __NVCC__
		//! Sorted domain particles domain or ghost
		openfpm::vector_gpu<aggregate<unsigned int>> isSortedIndexOrGhost(stop-start+1);

		auto ite = isSortedIndexOrGhost.getGPUIterator();

		CUDA_LAUNCH((mark_domain_particles),ite,
			sortedToUnsortedIndex.toKernel(),
			isSortedIndexOrGhost.toKernel(),
			ghostMarker
		);

		openfpm::scan(
			(unsigned int *)isSortedIndexOrGhost.template getDeviceBuffer<0>(),
			isSortedIndexOrGhost.size(),
			(unsigned int *)isSortedIndexOrGhost.template getDeviceBuffer<0>(),
			gpuContext
		);

		isSortedIndexOrGhost.template deviceToHost<0>(isSortedIndexOrGhost.size()-1,isSortedIndexOrGhost.size()-1);
		auto totalParticleNoGhostCount = isSortedIndexOrGhost.template get<0>(isSortedIndexOrGhost.size()-1);

		indexSorted.resize(totalParticleNoGhostCount);

		CUDA_LAUNCH((collect_domain_ghost_ids),ite,
			isSortedIndexOrGhost.toKernel(),
			indexSorted.toKernel()
		);
#endif
	}

public:

	//! Indicate that this cell list is a gpu type cell-list
	typedef int yes_is_gpu_celllist;

	// typedefs needed for toKernel_transform

	static const unsigned int dim_ = dim;
	typedef T stype_;
	typedef ids_type ids_type_;
	typedef transform_type transform_type_;
	typedef boost::mpl::bool_<true> is_sparse_;

	// end of typedefs needed for toKernel_transform

	/*! \brief Copy constructor
	 *
	 * \param clg Cell list to copy
	 *
	 */
	CellList_gpu(const CellList_gpu<dim,T,Memory,transform_type> & clg)
	{
		this->operator=(clg);
	}

	/*! \brief Copy constructor from temporal
	 *
	 *
	 *
	 */
	CellList_gpu(CellList_gpu<dim,T,Memory,transform_type> && clg)
	{
		this->operator=(clg);
	}

	/*! \brief default constructor
	 *
	 *
	 */
	CellList_gpu()
	{}

	CellList_gpu(
		const Box<dim,T> & box,
		const size_t (&div)[dim],
		const size_t pad = 1)
	{
		Initialize(box,div,pad);
	}

	void setBoxNN(unsigned int n_NN)
	{
		boxNeighborNumber = n_NN;
		constructNeighborCellOffset(n_NN);
	}

	void resetBoxNN()
	{
		constructNeighborCellOffset(boxNeighborNumber);
	}

	/*! Initialize the cell list constructor
	 *
	 * \param box Domain where this cell list is living
	 * \param div grid size on each dimension
	 * \param pad padding cell
	 * \param slot maximum number of slot
	 *
	 */
	void Initialize(
		const Box<dim,T> & box,
		const size_t (&div)[dim],
		const size_t pad = 1)
	{
		Matrix<dim,T> mat;
		CellDecomposer_sm<dim,T,transform_type>::setDimensions(box, div, mat, pad);

		// create the array that store the number of particle on each cell and se it to 0
		InitializeStructures(this->cellListGrid.getSize(),this->cellListGrid.size(),pad);
	}

	inline openfpm::vector_gpu<aggregate<unsigned int>> &
	getSortToNonSort() {
		return sortedToUnsortedIndex;
	}

	inline openfpm::vector_gpu<aggregate<unsigned int>> &
	getNonSortToSort() {
		return unsortedToSortedIndex;
	}

	inline openfpm::vector_gpu<aggregate<unsigned int>> &
	getDomainSortIds() {
		return indexSorted;
	}


	/*! \brief getNNIteratorRadius in CellList_gpu_ker.cuh implemented only for dense cell list
	 *
	 * \param radius
	 *
	 */
	void setRadius(T radius)
	{
		std::cerr << "setRadius() is supported by dense cell list only!\n";
	}

	/*! \brief construct from a list of particles
	 *
	 * \warning vPos is assumed to be already be in device memory
	 *
	 * \param vPos Particles list
	 *
	 */
	template<typename vector, typename vector_prp, unsigned int ... prp>
	void construct(
		vector & vPos,
		vector & vPosOut,
		vector_prp & vPrp,
		vector_prp & vPrpOut,
		gpu::ofp_context_t& gpuContext,
		size_t ghostMarker = 0,
		size_t start = 0,
		size_t stop = (size_t)-1,
		cl_construct_opt opt = cl_construct_opt::Full)
	{
		construct_sparse<vector,vector_prp,prp...>(vPos,vPosOut,vPrp,vPrpOut,gpuContext,ghostMarker,start,stop,opt);
	}

	CellList_gpu_ker<dim,T,ids_type,transform_type,true> toKernel()
	{
		return CellList_gpu_ker<dim,T,ids_type,transform_type,true>(
			neighborCellCount.toKernel(),
			neighborPartIndexFrom_To.toKernel(),
			vecSparseCellIndex_PartIndex.toKernel(),
			sortedToUnsortedIndex.toKernel(),
			indexSorted.toKernel(),
			unitCellP2,
			numCellDim,
			cellPadDim,
			this->getTransform(),
			ghostMarker,
			this->cellListSpaceBox,
			this->cellListGrid,
			this->cellShift
		);
	}

	/*! \brief Clear the structure
	 *
	 *
	 */
	void clear()
	{
		cellIndexLocalIndexToPart.clear();
		cellIndex.clear();
		sortedToUnsortedIndex.clear();
	}

	/////////////////////////////////////

	//! Ghost marker
	size_t ghostMarker = 0;

	/*! \brief return the ghost marker
	 *
	 * \return ghost marker
	 *
	 */
	inline size_t get_gm()
	{
		return ghostMarker;
	}

	/*! \brief Set the ghost marker
	 *
	 * \param ghostMarker marker
	 *
	 */
	inline void set_gm(size_t ghostMarker)
	{
		this->ghostMarker = ghostMarker;
	}

	/////////////////////////////////////

	/*! \brief Set the nDecRefRedec number
	 *
	 * \param nDecRefRedec
	 *
	 */
	void set_ndec(size_t nDecRefRedec)
	{
		this->nDecRefRedec = nDecRefRedec;
	}

	/*! \brief Set the nDecRefRedec number
	 *
	 * \return nDecRefRedec
	 *
	 */
	size_t get_ndec() const
	{
		return nDecRefRedec;
	}

	/////////////////////////////////////

	/*! \brief Transfer the information computed on gpu to construct the cell-list on gpu
	 *
	 */
	void debug_deviceToHost()
	{
		cellIndexLocalIndexToPart.template deviceToHost<0>();
		cellIndex.template deviceToHost<0>();
	}

	/*! \brief Get an element in the cell
	 *
	 * \tparam i property to get
	 *
	 * \param cell cell id
	 * \param ele element id
	 *
	 * \return The element value
	 *
	 */
	inline auto get(size_t cell, size_t ele) -> decltype(cellIndexLocalIndexToPart.template get<0>(cellIndex.template get<0>(cell)+ele))
	{
		return cellIndexLocalIndexToPart.template get<0>(cellIndex.template get<0>(cell)+ele);
	}

	/*! \brief Get an element in the cell
	 *
	 * \tparam i property to get
	 *
	 * \param cell cell id
	 * \param ele element id
	 *
	 * \return The element value
	 *
	 */
	inline auto get(size_t cell, size_t ele) const -> decltype(cellIndexLocalIndexToPart.template get<0>(cellIndex.template get<0>(cell)+ele))
	{
		return cellIndexLocalIndexToPart.template get<0>(cellIndex.template get<0>(cell)+ele);
	}

	/*! \brief swap the information of the two cell-lists
	 *
	 *
	 *
	 */
	void swap(CellList_gpu<dim,T,Memory,transform_type,true> & clg)
	{
		((CellDecomposer_sm<dim,T,transform_type> *)this)->swap(clg);
		cellIndexLocalIndexToPart.swap(clg.cellIndexLocalIndexToPart);
		cellIndex.swap(clg.cellIndex);
		vecSparseCellIndex_PartIndex.swap(clg.vecSparseCellIndex_PartIndex);
		neighborCellCount.swap(clg.neighborCellCount);
		neighborPartIndexFrom_To.swap(clg.neighborPartIndexFrom_To);
		boxNeighborCellOffset.swap(clg.boxNeighborCellOffset);
		sortedToUnsortedIndex.swap(clg.sortedToUnsortedIndex);
		indexSorted.swap(clg.indexSorted);
		unsortedToSortedIndex.swap(clg.unsortedToSortedIndex);

		unitCellP2.swap(clg.unitCellP2);
		numCellDim.swap(clg.numCellDim);
		cellPadDim.swap(clg.cellPadDim);

		size_t g_m_tmp = ghostMarker;
		ghostMarker = clg.ghostMarker;
		clg.ghostMarker = g_m_tmp;

		size_t n_dec_tmp = nDecRefRedec;
		nDecRefRedec = clg.nDecRefRedec;
		clg.nDecRefRedec = n_dec_tmp;

		int boxNN_tmp = boxNeighborNumber;
		boxNeighborNumber = clg.boxNeighborNumber;
		clg.boxNeighborNumber = boxNN_tmp;
	}

	CellList_gpu<dim,T,Memory,transform_type,true> &
	operator=(const CellList_gpu<dim,T,Memory,transform_type,true> & clg)
	{
		*static_cast<CellDecomposer_sm<dim,T,transform_type> *>(this) = *static_cast<const CellDecomposer_sm<dim,T,transform_type> *>(&clg);
		cellIndexLocalIndexToPart = clg.cellIndexLocalIndexToPart;
		cellIndex = clg.cellIndex;
		vecSparseCellIndex_PartIndex = clg.vecSparseCellIndex_PartIndex;
		neighborCellCount = clg.neighborCellCount;
		neighborPartIndexFrom_To = clg.neighborPartIndexFrom_To;
		boxNeighborCellOffset = clg.boxNeighborCellOffset;
		sortedToUnsortedIndex = clg.sortedToUnsortedIndex;
		indexSorted = clg.indexSorted;
		unsortedToSortedIndex = clg.unsortedToSortedIndex;

		unitCellP2 = clg.unitCellP2;
		numCellDim = clg.numCellDim;
		cellPadDim = clg.cellPadDim;
		ghostMarker = clg.ghostMarker;
		nDecRefRedec = clg.nDecRefRedec;

		boxNeighborNumber = clg.boxNeighborNumber;

		return *this;
	}

	CellList_gpu<dim,T,Memory,transform_type> &
	operator=(CellList_gpu<dim,T,Memory,transform_type> && clg)
	{
		static_cast<CellDecomposer_sm<dim,T,transform_type> *>(this)->swap(*static_cast<CellDecomposer_sm<dim,T,transform_type> *>(&clg));
		cellIndexLocalIndexToPart.swap(clg.cellIndexLocalIndexToPart);
		cellIndex.swap(clg.cellIndex);
		vecSparseCellIndex_PartIndex.swap(clg.vecSparseCellIndex_PartIndex);
		neighborCellCount.swap(clg.neighborCellCount);
		neighborPartIndexFrom_To.swap(clg.neighborPartIndexFrom_To);
		boxNeighborCellOffset.swap(clg.boxNeighborCellOffset);
		sortedToUnsortedIndex.swap(clg.sortedToUnsortedIndex);
		indexSorted.swap(clg.indexSorted);
		unsortedToSortedIndex.swap(clg.unsortedToSortedIndex);

		unitCellP2 = clg.unitCellP2;
		numCellDim = clg.numCellDim;
		cellPadDim = clg.cellPadDim;
		ghostMarker = clg.ghostMarker;
		nDecRefRedec = clg.nDecRefRedec;

		boxNeighborNumber = clg.boxNeighborNumber;

		return *this;
	}
};

// This is a tranformation node for vector_distributed for the algorithm toKernel_tranform
template<template <typename> class layout_base, typename T>
struct toKernel_transform<layout_base,T,4>
{
	typedef CellList_gpu_ker<T::dim_,
		typename T::stype_,
		typename T::ids_type_,
		typename T::transform_type_,
		T::is_sparse_::value> type;
};

#endif

#endif /* OPENFPM_DATA_SRC_NN_CELLLIST_CELLLIST_GPU_HPP_ */
