#ifndef CELLLIST_GPU_KER_CUH_
#define CELLLIST_GPU_KER_CUH_

#include "NN/CellList/cuda/CellDecomposer_gpu_ker.cuh"

template<unsigned int dim, typename ids_type, bool symmetric>
class NN_gpu_it_box;

template<unsigned int dim, typename ids_type>
class NN_gpu_it_box<dim, ids_type, false>
{
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum;
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex;
	const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & boxNeighborCellOffset;

	int neighborCellIndexAct;
	unsigned int boxNeighborCellOffset_i;
	unsigned int cellPositionIndex;
	unsigned int neighborPartIndexStart;
	unsigned int neighborPartIndexStop;

	inline __device__ void SelectValid()
	{
		while (neighborPartIndexStart == neighborPartIndexStop && isNext())
		{
			this->nextCell();

			if (isNext() == false) break;

			neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct);
			neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct+1);
		}
	}

public:
	inline __device__ NN_gpu_it_box(
		const grid_key_dx<dim,ids_type> & cellPosition,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex,
		const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & boxNeighborCellOffset,
		const openfpm::array<ids_type,dim> & numCellDim)
	: numPartInCellPrefixSum(numPartInCellPrefixSum),
	sortedToUnsortedIndex(sortedToUnsortedIndex),
	boxNeighborCellOffset(boxNeighborCellOffset),
	cellPositionIndex(cid_<dim,ids_type,int>::get_cid(numCellDim,cellPosition)),
	boxNeighborCellOffset_i(0)
	{
		while (cellPositionIndex+this->boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i) < 0
			|| cellPositionIndex+this->boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i) >= numPartInCellPrefixSum.size())
		{
			++boxNeighborCellOffset_i;
		}

		neighborCellIndexAct = boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i);
		neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct);
		neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct+1);

		SelectValid();
	}

	inline __device__ unsigned int get_sort()
	{
		return neighborPartIndexStart;
	}

	inline __device__ unsigned int get()
	{
		return sortedToUnsortedIndex.template get<0>(neighborPartIndexStart);
	}

	inline __device__ NN_gpu_it_box<dim,ids_type,false> & operator++()
	{
		++neighborPartIndexStart; SelectValid();

		return *this;
	}

	inline __device__ unsigned int get_start(unsigned int ce_id)
	{
		return numPartInCellPrefixSum.template get<0>(ce_id);
	}

	inline __device__ unsigned int get_cid()
	{
		return cellPositionIndex + neighborCellIndexAct;
	}

	inline __device__ bool isNext()
	{
		return boxNeighborCellOffset_i < boxNeighborCellOffset.size();
	}

	__device__ inline void nextCell()
	{
		++boxNeighborCellOffset_i;

		do {
			neighborCellIndexAct = boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i);
		} while ((cellPositionIndex + neighborCellIndexAct < 0
			|| cellPositionIndex + neighborCellIndexAct + 1 >= (numPartInCellPrefixSum.size()))
			&& boxNeighborCellOffset_i < boxNeighborCellOffset.size());
	}

};

template<unsigned int dim, typename ids_type>
class NN_gpu_it_box<dim, ids_type, true>
{
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum;
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex;
	const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & boxNeighborCellOffset;

	unsigned int partUnsortedIndex;
	int neighborCellIndexAct;
	unsigned int boxNeighborCellOffset_i;
	unsigned int cellPositionIndex;
	unsigned int neighborPartIndexStart;
	unsigned int neighborPartIndexStop;

	inline __device__ void SelectValid()
	{
		for (;;) {
			while (partUnsortedIndex > sortedToUnsortedIndex.template get<0>(neighborPartIndexStart) && neighborPartIndexStart < neighborPartIndexStop) {
				++neighborPartIndexStart;
			}
			if ((neighborPartIndexStart < neighborPartIndexStop)) {
				break;
			}
			while (neighborPartIndexStart == neighborPartIndexStop && isNext())
			{
				this->nextCell();

				if (isNext() == false) break;

				neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct);
				neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct+1);
			}
			if (isNext() == false) break;
		}
	}

public:
	inline __device__ NN_gpu_it_box(
		const size_t partUnsortedIndex,
		const grid_key_dx<dim,ids_type> & cellPosition,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex,
		const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & boxNeighborCellOffset,
		const openfpm::array<ids_type,dim> & numCellDim)
	: partUnsortedIndex(partUnsortedIndex),
	numPartInCellPrefixSum(numPartInCellPrefixSum),
	sortedToUnsortedIndex(sortedToUnsortedIndex),
	boxNeighborCellOffset(boxNeighborCellOffset),
	cellPositionIndex(cid_<dim,ids_type,int>::get_cid(numCellDim,cellPosition)),
	boxNeighborCellOffset_i(boxNeighborCellOffset.size() / 2)
	//boxNeighborCellOffset_i(0)
	{
		while (cellPositionIndex+this->boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i) < 0
			|| cellPositionIndex+this->boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i) >= numPartInCellPrefixSum.size())
		{
			++boxNeighborCellOffset_i;
		}

		neighborCellIndexAct = boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i);
		neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct);
		neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct+1);

		SelectValid();
	}

	inline __device__ unsigned int get_sort()
	{
		return neighborPartIndexStart;
	}

	inline __device__ unsigned int get()
	{
		return sortedToUnsortedIndex.template get<0>(neighborPartIndexStart);
	}

	inline __device__ NN_gpu_it_box<dim,ids_type,true> & operator++()
	{
		++neighborPartIndexStart; SelectValid();

		return *this;
	}

	inline __device__ unsigned int get_start(unsigned int ce_id)
	{
		return numPartInCellPrefixSum.template get<0>(ce_id);
	}

	inline __device__ unsigned int get_cid()
	{
		return cellPositionIndex + neighborCellIndexAct;
	}

	inline __device__ bool isNext()
	{
		return boxNeighborCellOffset_i < boxNeighborCellOffset.size();
	}

	__device__ inline void nextCell()
	{
		++boxNeighborCellOffset_i;

		do {
			neighborCellIndexAct = boxNeighborCellOffset.template get<0>(boxNeighborCellOffset_i);
		} while ((cellPositionIndex + neighborCellIndexAct < 0
			|| cellPositionIndex + neighborCellIndexAct + 1 >= (numPartInCellPrefixSum.size()))
			&& boxNeighborCellOffset_i < boxNeighborCellOffset.size());
	}

};

template<unsigned int dim, typename ids_type, bool symmetric>
class NN_gpu_it_radius;

template<unsigned int dim, typename ids_type>
class NN_gpu_it_radius<dim, ids_type, false>
{
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum;
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex;
	const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & rcutNeighborCellOffset;

	int neighborCellIndexAct;
	unsigned int rcutNeighborCellOffset_i;
	unsigned int cellPositionIndex;
	unsigned int neighborPartIndexStart;
	unsigned int neighborPartIndexStop;

	inline __device__ void SelectValid()
	{
		while (neighborPartIndexStart == neighborPartIndexStop && isNext())
		{
			this->nextCell();

			if (isNext() == false) {break;}

			neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct);
			neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct+1);
		}
	}

public:
	inline __device__ __host__ NN_gpu_it_radius(
		const grid_key_dx<dim,ids_type> & cellPosition,
		const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & rcutNeighborCellOffset,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex,
		const openfpm::array<ids_type,dim> & numCellDim)
	: numPartInCellPrefixSum(numPartInCellPrefixSum),
	sortedToUnsortedIndex(sortedToUnsortedIndex),
	rcutNeighborCellOffset(rcutNeighborCellOffset),
	cellPositionIndex(cid_<dim,ids_type,int>::get_cid(numCellDim,cellPosition)),
	rcutNeighborCellOffset_i(0)
	{
		while (cellPositionIndex+rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i) < 0
			|| cellPositionIndex+rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i) >= numPartInCellPrefixSum.size())
		{
			++rcutNeighborCellOffset_i;
		}

		neighborCellIndexAct = rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i);
		neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct);
		neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct+1);

		SelectValid();
	}

	inline __device__ unsigned int get_sort()
	{
		return neighborPartIndexStart;
	}

	inline __device__ unsigned int get()
	{
		return sortedToUnsortedIndex.template get<0>(neighborPartIndexStart);
	}

	inline __device__ NN_gpu_it_radius<dim,ids_type,false> & operator++()
	{
		++neighborPartIndexStart; SelectValid();

		return *this;
	}

	inline __device__ unsigned int get_start(unsigned int ce_id)
	{
		return numPartInCellPrefixSum.template get<0>(ce_id);
	}

	inline __device__ unsigned int get_cid()
	{
		return cellPositionIndex+neighborCellIndexAct;
	}

	inline __device__ bool isNext()
	{
		return rcutNeighborCellOffset_i < rcutNeighborCellOffset.size();
	}

	__device__ inline void nextCell()
	{
		++rcutNeighborCellOffset_i;

		do {
			neighborCellIndexAct = rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i);
		} while ((cellPositionIndex + neighborCellIndexAct < 0
			|| cellPositionIndex + neighborCellIndexAct + 1 >= (numPartInCellPrefixSum.size()))
			&& rcutNeighborCellOffset_i < rcutNeighborCellOffset.size());
	}

};

template<unsigned int dim, typename ids_type>
class NN_gpu_it_radius<dim, ids_type, true>
{
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum;
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex;
	const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & rcutNeighborCellOffset;

	unsigned int partUnsortedIndex;
	int neighborCellIndexAct;
	unsigned int rcutNeighborCellOffset_i;
	unsigned int cellPositionIndex;
	unsigned int neighborPartIndexStart;
	unsigned int neighborPartIndexStop;

	inline __device__ void SelectValid()
	{
		for (;;) {
			while (partUnsortedIndex > sortedToUnsortedIndex.template get<0>(neighborPartIndexStart) && neighborPartIndexStart < neighborPartIndexStop) {
				++neighborPartIndexStart;
			}
			if ((neighborPartIndexStart < neighborPartIndexStop)) {
				break;
			}
			while (neighborPartIndexStart == neighborPartIndexStop && isNext())
			{
				this->nextCell();

				if (isNext() == false) break;

				neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct);
				neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+this->neighborCellIndexAct+1);
			}
			if (isNext() == false) break;
		}
	}

public:
	inline __device__ __host__ NN_gpu_it_radius(
		const size_t partUnsortedIndex,
		const grid_key_dx<dim,ids_type> & cellPosition,
		const openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> & rcutNeighborCellOffset,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & numPartInCellPrefixSum,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex,
		const openfpm::array<ids_type,dim> & numCellDim)
	: partUnsortedIndex(partUnsortedIndex),
	numPartInCellPrefixSum(numPartInCellPrefixSum),
	sortedToUnsortedIndex(sortedToUnsortedIndex),
	rcutNeighborCellOffset(rcutNeighborCellOffset),
	cellPositionIndex(cid_<dim,ids_type,int>::get_cid(numCellDim,cellPosition)),
	rcutNeighborCellOffset_i(rcutNeighborCellOffset.size() / 2)
	{
		while (cellPositionIndex+rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i) < 0
			|| cellPositionIndex+rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i) >= numPartInCellPrefixSum.size())
		{
			++rcutNeighborCellOffset_i;
		}

		neighborCellIndexAct = rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i);
		neighborPartIndexStart = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct);
		neighborPartIndexStop = numPartInCellPrefixSum.template get<0>(cellPositionIndex+neighborCellIndexAct+1);

		SelectValid();
	}

	inline __device__ unsigned int get_sort()
	{
		return neighborPartIndexStart;
	}

	inline __device__ unsigned int get()
	{
		return sortedToUnsortedIndex.template get<0>(neighborPartIndexStart);
	}

	inline __device__ NN_gpu_it_radius<dim,ids_type,true> & operator++()
	{
		++neighborPartIndexStart; SelectValid();

		return *this;
	}

	inline __device__ unsigned int get_start(unsigned int ce_id)
	{
		return numPartInCellPrefixSum.template get<0>(ce_id);
	}

	inline __device__ unsigned int get_cid()
	{
		return cellPositionIndex+neighborCellIndexAct;
	}

	inline __device__ bool isNext()
	{
		return rcutNeighborCellOffset_i < rcutNeighborCellOffset.size();
	}

	__device__ inline void nextCell()
	{
		++rcutNeighborCellOffset_i;

		do {
			neighborCellIndexAct = rcutNeighborCellOffset.template get<0>(rcutNeighborCellOffset_i);
		} while ((cellPositionIndex + neighborCellIndexAct < 0
			|| cellPositionIndex + neighborCellIndexAct + 1 >= (numPartInCellPrefixSum.size()))
			&& rcutNeighborCellOffset_i < rcutNeighborCellOffset.size());
	}

};

template<unsigned int dim, typename T, typename ids_type, typename transform_type, bool is_sparse>
class CellList_gpu_ker: public CellDecomposer_gpu_ker<dim,T,ids_type,transform_type>
{
	//! starting point for each cell
	openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> numPartInCellPrefixSum;

	//! Sorted to non sorted ids conversion
	openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToUnsortedIndex;

	//! Domain particles ids
	openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToSortedIndexNoGhost;

	//! radius cells
	openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> rcutNeighborCellOffset;

	//! Box cells
	openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> boxNeighborCellOffset;

	//! Ghost particle marker
	unsigned int ghostMarker;

public:

	typedef int yes_is_gpu_ker_celllist;

	//! Indicate this structure has a function to check the device pointer
	typedef int yes_has_check_device_pointer;

	__host__ __device__ inline CellList_gpu_ker()
	:ghostMarker(0)
	{}

	__host__ __device__ inline CellList_gpu_ker(
		openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> numPartInCellPrefixSum,
		openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToUnsortedIndex,
		openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToSortedIndexNoGhost,
		openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> rcutNeighborCellOffset,
		openfpm::vector_gpu_ker<aggregate<int>,memory_traits_inte> boxNeighborCellOffset,
		openfpm::array<T,dim> unitCellP2,
		openfpm::array<ids_type,dim> & numCellDim,
		openfpm::array<ids_type,dim> & cellPadDim,
		const transform_type & pointTransform,
		unsigned int ghostMarker,
		Box<dim,T> cellListSpaceBox,
		grid_sm<dim,void> cellListGrid,
		Point<dim,long int> cellShift)
	: CellDecomposer_gpu_ker<dim,T,ids_type,transform_type>(unitCellP2,numCellDim,cellPadDim,pointTransform,cellListSpaceBox,cellListGrid,cellShift),
	numPartInCellPrefixSum(numPartInCellPrefixSum),
	sortedToUnsortedIndex(sortedToUnsortedIndex),
	sortedToSortedIndexNoGhost(sortedToSortedIndexNoGhost),
	rcutNeighborCellOffset(rcutNeighborCellOffset),
	boxNeighborCellOffset(boxNeighborCellOffset),
	ghostMarker(ghostMarker)
	{}

	inline __device__ NN_gpu_it_radius<dim,ids_type,false> getNNIteratorRadius(
		const grid_key_dx<dim,ids_type> & cellPosition)
	{
		NN_gpu_it_radius<dim,ids_type,false> ngi(cellPosition,rcutNeighborCellOffset,numPartInCellPrefixSum,sortedToUnsortedIndex,this->get_div_c());

		return ngi;
	}

	inline __device__ NN_gpu_it_radius<dim,ids_type,true> getNNIteratorRadiusSym(
		const size_t partUnsortedIndex, const grid_key_dx<dim,ids_type> & cellPosition)
	{
		NN_gpu_it_radius<dim,ids_type,true> ngi(partUnsortedIndex,cellPosition,rcutNeighborCellOffset,numPartInCellPrefixSum,sortedToUnsortedIndex,this->get_div_c());

		return ngi;
	}

	inline __device__ NN_gpu_it_box<dim,ids_type,false> getNNIteratorBox(
		const grid_key_dx<dim,ids_type> & cellPosition)
	{
		NN_gpu_it_box<dim,ids_type,false> ngi(cellPosition,numPartInCellPrefixSum,sortedToUnsortedIndex,boxNeighborCellOffset,this->get_div_c());

		return ngi;
	}

	inline __device__ NN_gpu_it_box<dim,ids_type,true> getNNIteratorBoxSym(
		const size_t partUnsortedIndex, const grid_key_dx<dim,ids_type> & cellPosition)
	{
		NN_gpu_it_box<dim,ids_type,true> ngi(partUnsortedIndex,cellPosition,numPartInCellPrefixSum,sortedToUnsortedIndex,boxNeighborCellOffset,this->get_div_c());

		return ngi;
	}

	inline __device__ auto getNNIterator(
		const grid_key_dx<dim,ids_type> & cellPosition) -> decltype(this->getNNIteratorBox(cellPosition))
	{
		return this->getNNIteratorBox(cellPosition);
	}

	inline __device__ openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & getDomainSortIds()
	{
		return sortedToSortedIndexNoGhost;
	}

	inline __device__ openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & getSortToNonSort()
	{
		return sortedToUnsortedIndex;
	}

	/*! \brief Get the number of cells this cell-list contain
	 *
	 * \return number of cells
	 */
	inline __device__ unsigned int getNCells() const
	{
		return numPartInCellPrefixSum.size() - 1;
	}

	/*! \brief Return the number of elements in the cell
	 *
	 * \param cell_id id of the cell
	 *
	 * \return number of elements in the cell
	 *
	 */
	inline __device__ unsigned int getNelements(unsigned int cell_id) const
	{
		return numPartInCellPrefixSum.template get<0>(cell_id+1) - numPartInCellPrefixSum.template get<0>(cell_id);
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
	inline __device__ unsigned int get(
		unsigned int cell, 
		unsigned int ele)
	{
		unsigned int p_id = numPartInCellPrefixSum.template get<0>(cell) + ele;
		return sortedToUnsortedIndex.template get<0>(p_id);
	}


	inline __device__ unsigned int getGhostMarker()
	{
		return ghostMarker;
	}

#ifdef SE_CLASS1

	/*! \brief Check if the device pointer is owned by this structure
	 *
	 * \return a structure pointer check with information about the match
	 *
	 */
	pointer_check check_device_pointer(void * ptr)
	{
		pointer_check pc;

		pc = numPartInCellPrefixSum.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Cell index overflow (numPartInCellPrefixSum): ") + "\n" + pc.match_str;
			return pc;
		}

		pc = sortedToUnsortedIndex.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Particle index overflow (str): ") + "\n" + pc.match_str;
			return pc;
		}

		pc = sortedToSortedIndexNoGhost.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Particle index overflow (sortedToSortedIndexNoGhost): ") + "\n" + pc.match_str;
			return pc;
		}

		pc = rcutNeighborCellOffset.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Particle index overflow (sortedToSortedIndexNoGhost): ") + "\n" + pc.match_str;
			return pc;
		}

		return pc;
	}

#endif
};

template<unsigned int dim, typename ids_type>
class NN_gpu_it_sparse
{
	unsigned int neighborPartIndexStart;
	unsigned int neighborPartIndexStop;
	unsigned int neighborCellIndexStart;
	unsigned int neighborCellIndexStop;

	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex;
	const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & neighborCellCountPrefixSum;
	const openfpm::vector_gpu_ker<aggregate<unsigned int,unsigned int>,memory_traits_inte> & neighborPartIndexFrom_To;

	__device__ void SelectValid()
	{
		while ((neighborPartIndexStart == neighborPartIndexStop) && isNext())
		{
			++neighborCellIndexStart;

			if (neighborCellIndexStart < neighborCellIndexStop)
			{
				neighborPartIndexStart = neighborPartIndexFrom_To.template get<0>(neighborCellIndexStart);
				neighborPartIndexStop = neighborPartIndexFrom_To.template get<1>(neighborCellIndexStart);
			}
		}
	}

public:
	__device__ NN_gpu_it_sparse(
		unsigned int cellIndex,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & neighborCellCountPrefixSum,
		const openfpm::vector_gpu_ker<aggregate<unsigned int,unsigned int>,memory_traits_inte> & neighborPartIndexFrom_To,
		const openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & sortedToUnsortedIndex)
	: sortedToUnsortedIndex(sortedToUnsortedIndex),
	neighborCellCountPrefixSum(neighborCellCountPrefixSum),
	neighborPartIndexFrom_To(neighborPartIndexFrom_To)
	{
		if (cellIndex == (unsigned int)-1)
		{
			neighborCellIndexStop = neighborCellIndexStart;
			return;
		}

		neighborCellIndexStart = neighborCellCountPrefixSum.template get<0>(cellIndex);
		neighborCellIndexStop = neighborCellCountPrefixSum.template get<0>(cellIndex + 1);

		neighborPartIndexStart = neighborPartIndexFrom_To.template get<0>(neighborCellIndexStart);
		neighborPartIndexStop = neighborPartIndexFrom_To.template get<1>(neighborCellIndexStart);

		SelectValid();
	}

	__device__ unsigned int get_sort()
	{
		return neighborPartIndexStart;
	}

	__device__ unsigned int get()
	{
		return sortedToUnsortedIndex.template get<0>(neighborPartIndexStart);
	}

	__device__ NN_gpu_it_sparse<dim,ids_type> & operator++()
	{
		++neighborPartIndexStart; SelectValid();

		return *this;
	}

	inline __device__ bool isNext()
	{
		return neighborCellIndexStart < neighborCellIndexStop;
	}
};

template<unsigned int dim, typename T, typename ids_type, typename transform_type>
class CellList_gpu_ker<dim,T,ids_type,transform_type,true>: public CellDecomposer_gpu_ker<dim,T,ids_type,transform_type>
{
	//! starting point for each cell
	openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> neighborCellCountPrefixSum;

	//! starting point for each cell
	openfpm::vector_gpu_ker<aggregate<unsigned int, unsigned int>,memory_traits_inte> neighborPartIndexFrom_To;

	//! Sorted to non sorted ids conversion
	openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToUnsortedIndex;

	//! Domain particles ids
	openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToSortedIndexNoGhost;

	//! Set of cells sparse
	openfpm::vector_sparse_gpu_ker<aggregate<unsigned int>,int,memory_traits_inte> vecSparseCellIndex_PartIndex;

	//! Ghost particle marker
	unsigned int ghostMarker;

public:

	//! Indicate this structure has a function to check the device pointer
	typedef int yes_has_check_device_pointer;

	__host__ __device__ inline CellList_gpu_ker(
		openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> neighborCellCountPrefixSum,
		openfpm::vector_gpu_ker<aggregate<unsigned int, unsigned int>,memory_traits_inte> neighborPartIndexFrom_To,
		openfpm::vector_sparse_gpu_ker<aggregate<unsigned int>,int,memory_traits_inte> vecSparseCellIndex_PartIndex,
		openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToUnsortedIndex,
		openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> sortedToSortedIndexNoGhost,
		openfpm::array<T,dim> & unitCellP2,
		openfpm::array<ids_type,dim> & numCellDim,
		openfpm::array<ids_type,dim> & cellPadDim,
		const transform_type & pointTransform,
		unsigned int ghostMarker,
		Box<dim,T> cellListSpaceBox,
		grid_sm<dim,void> cellListGrid,
		Point<dim,long int> cellShift
	)
	: CellDecomposer_gpu_ker<dim,T,ids_type,transform_type>(unitCellP2,numCellDim,cellPadDim,pointTransform,cellListSpaceBox,cellListGrid,cellShift),
	neighborCellCountPrefixSum(neighborCellCountPrefixSum),
	neighborPartIndexFrom_To(neighborPartIndexFrom_To),
	sortedToUnsortedIndex(sortedToUnsortedIndex),
	sortedToSortedIndexNoGhost(sortedToSortedIndexNoGhost),
	vecSparseCellIndex_PartIndex(vecSparseCellIndex_PartIndex),
	ghostMarker(ghostMarker)
	{}

	inline __device__ auto getCell(
		const Point<dim,T> & xp) 
	const -> decltype(vecSparseCellIndex_PartIndex.get_sparse(0))
	{
		unsigned int cell = cid_<dim,ids_type,transform_type>::get_cid(this->get_div_c(),this->get_spacing_c(),this->get_off(),this->get_t(),xp);

		return vecSparseCellIndex_PartIndex.get_sparse(cell);
	}

	inline __device__ NN_gpu_it_sparse<dim,ids_type> getNNIterator(
		decltype(vecSparseCellIndex_PartIndex.get_sparse(0)) cId)
	{
		NN_gpu_it_sparse<dim,ids_type> ngi(cId.id,neighborCellCountPrefixSum,neighborPartIndexFrom_To,sortedToUnsortedIndex);

		return ngi;
	}

	inline __device__ NN_gpu_it_sparse<dim,ids_type> getNNIteratorBox(
		decltype(vecSparseCellIndex_PartIndex.get_sparse(0)) cId)
	{
		NN_gpu_it_sparse<dim,ids_type> ngi(cId.id,neighborCellCountPrefixSum,neighborPartIndexFrom_To,sortedToUnsortedIndex);

		return ngi;
	}


	inline __device__ openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & getDomainSortIds()
	{
		return sortedToSortedIndexNoGhost;
	}

	inline __device__ openfpm::vector_gpu_ker<aggregate<unsigned int>,memory_traits_inte> & getSortToNonSort()
	{
		return sortedToUnsortedIndex;
	}


	inline __device__ unsigned int getGhostMarker()
	{
		return ghostMarker;
	}

#ifdef SE_CLASS1

	/*! \brief Check if the device pointer is owned by this structure
	 *
	 * \return a structure pointer check with information about the match
	 *
	 */
	pointer_check check_device_pointer(void * ptr)
	{
		pointer_check pc;

		pc = neighborCellCountPrefixSum.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Cell index overflow (starts): ") + "\n" + pc.match_str;
			return pc;
		}

		pc = neighborPartIndexFrom_To.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Cell particle buffer overflow (neighborPartIndexFrom_To): ") + "\n" + pc.match_str;
			return pc;
		}

		pc = sortedToUnsortedIndex.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Particle index overflow (str): ") + "\n" + pc.match_str;
			return pc;
		}

		pc = sortedToSortedIndexNoGhost.check_device_pointer(ptr);

		if (pc.match == true)
		{
			pc.match_str = std::string("Particle index overflow (sortedToSortedIndexNoGhost): ") + "\n" + pc.match_str;
			return pc;
		}

		return pc;
	}

#endif
};


#endif /* CELLLIST_GPU_KER_CUH_ */
