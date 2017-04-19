#include <stdio.h>
#include <string.h>
#include <utility>
using namespace std;

// Assume all inputs are invalid.
// Assume n is the number of elements to be sorted.

// TC: Best = O(n^2), Average = O(n^2), Worst = O(n^2)
// SC: O(1)
void SelectionSort(int *data, int first, int last)
{
	// [first, first_unsorted) is sorted, [first_unsorted, last) is unsorted.
	for(int first_unsorted = first; first_unsorted < last; ++first_unsorted)
	{
		int min_index = first_unsorted;
		for(int compare_index = first_unsorted + 1; compare_index < last; ++compare_index)
		{
			if(data[min_index] > data[compare_index])
			{
				min_index = compare_index;
			}
		}
		if(min_index != first_unsorted)
		{
			swap(data[min_index], data[first_unsorted]);
		}
		// One more element is sorted:
		// [first, first_unsorted + 1) is sorted, [first_unsorted + 1, last) is unsorted.
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TC: Best = O(n), Average = O(n^2), Worst = O(n^2)
// SC: O(1)
void BubbleSort(int *data, int first, int last) // [first, last)
{
	// [first, first + unsorted_number) is unsorted
	// [first + unsorted_number, last) is sorted.
	for(int unsorted_number = last - first; unsorted_number > 0; )
	{
		int last_swap_index = -1;
		// Traverse [first, first + unsorted_number)
		for(int latter_index = first + 1;
		        latter_index < first + unsorted_number;
		        ++latter_index)
		{
			if(data[latter_index - 1] > data[latter_index])
			{
				swap(data[latter_index - 1], data[latter_index]);
				last_swap_index = latter_index;
			}
		}
		// [first, last_swap_index) is unsorted, [last_swap_index, last) is sorted.
		unsorted_number = last_swap_index - first;
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TC: Best = O(n), Average = O(n^2), Worst = O(n^2)
// SC: O(1)
void InsertionSort(int *data, int first, int last)
{
	// [first, first_unsorted) is sorted, [first_unsorted, last) is unsorted.
	for(int first_unsorted = first; first_unsorted < last; ++first_unsorted)
	{
		for(int latter_index = first_unsorted;
		        latter_index - 1 >= first && data[latter_index - 1] > data[latter_index];
		        --latter_index)
		{
			swap(data[latter_index - 1], data[latter_index]);
		}
		// One more element is sorted.
		// [first, first_unsorted + 1) is sorted; [first_unsorted + 1, last) is unsorted.
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int Partition(int *data, int first, int last) // O(n)
{
	int pivot = data[last - 1];
	int not_greater_number = 0;
	for(int index = first; index < last - 1; ++index)
	{
		if(data[index] <= pivot)
		{
			++not_greater_number;
			if(index != first + not_greater_number - 1)
			{
				swap(data[index], data[first + not_greater_number - 1]);
			}
		}
	}
	if(first + not_greater_number != last - 1)
	{
		swap(data[first + not_greater_number], data[last - 1]);
	}
	return first + not_greater_number;
}
// TC: Best = O(nlogn), Average = O(nlogn), Worst = O(n^2)
// SC: Best = O(logn), Worst = O(n)
void QuickSort(int *data, int first, int last) // [first, last)
{
	if(last - first <= 1) // Only one element is auto sorted.
	{
		return;
	}
	// Divide: [first, divide) <= [divide, divide + 1) < [divide + 1, last)
	int divide = Partition(data, first, last);
	// Conquer: sort [first, divide) and [divide + 1, last) by recursive calls.
	QuickSort(data, first, divide);
	QuickSort(data, divide + 1, last);
	// Combine: sub arrays are sorted, no work is needed to combine them.
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Merge(int *data, int first, int middle, int last, int *helper) // O(n)
{
	int left = first; // index in left-subrange [first, middle).
	int right = middle; // index in right-subrange [middle, last).
	int helper_index = first; // index in helper.
	while(left < middle || right < last)  // Either subrange is not empty
	{
		// Copy left element into helper when:
		// 1.	Left subrange is not empty and right subrange is empty:
		//		`left < middle && right >= last`
		// 2.	Both are not empty and left element is lesser than or equal to right element:
		//		`left < middle && right < last && data[left] <= data[right]`
		// NOTE:
		// 1. `||` is short-circuit.
		// 2. `<=` keep the relative order of equal elements, thus guarantee sorting stability.
		if(right >= last || (left < middle && data[left] <= data[right]))
		{
			helper[helper_index++] = data[left++];
		}
		else
		{
			helper[helper_index++] = data[right++];
		}
	}
	// Copy sorted elements into data.
	for(int index = first; index < last; ++index)
	{
		data[index] = helper[index];
	}
}
void MergeSortMain(int *data, int first, int last, int *helper)
{
	if(last - first <= 1) // Only 1 element is auto sorted.
	{
		return;
	}
	// Divide: divide n-element array into two n/2-element subarrays.
	int middle = first + (last - first) / 2;
	// Conquer: sort two subarrays [first, middle) and [middle, last) recursively.
	MergeSortMain(data, first, middle, helper);
	MergeSortMain(data, middle, last, helper);
	// Combine: merge two sorted subarrays.
	Merge(data, first, middle, last, helper);
}
// TC: Best = O(nlogn), Average = O(nlogn), Worst = O(nlogn)
// SC: O(n)
void MergeSort(int *data, int first, int last) // [first, last)
{
	int helper[last]; // helper temporary stores the sorted subarrays in Merge.
	MergeSortMain(data, first, last, helper);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void MinHeapFixDown(int *data, int parent_index, int last)
{
	int max_child_index = parent_index * 2 + 1;
	while(max_child_index < last)
	{
		if(max_child_index < last - 1 &&
		        data[max_child_index] < data[max_child_index + 1])
		{
			++max_child_index;
		}
		if(data[parent_index] >= data[max_child_index]) // `=` guarantee stable.
		{
			return;
		}
		std::swap(data[parent_index], data[max_child_index]);
		parent_index = max_child_index;
		max_child_index = parent_index * 2 + 1;
	}
}
// TC: Best = O(nlogn), Average = O(nlogn), Worst = O(nlogn)
// SC: O(1)
void HeapSort(int *data, int first, int last)// 改成正序！！！
{
	// 1. Convert array to min heap. [first, first + length/2 - 1] has children.
	// O(n): See ITA Page 159
	for(int parent_index = first + (last - first) / 2 - 1; parent_index >= first; --parent_index)
	{
		MinHeapFixDown(data, parent_index, last);
	}
	// 2. Extract min. O(nlogn)
	for(int index = last - 1; index >= first; --index)
	{
		if(data[first] != data[index]) // Guarantee stable.
		{
			std::swap(data[first], data[index]);
			MinHeapFixDown(data, first, index);
		}
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// n is the number of inputs, m is the max value.
// TC: Best = Average = Worst = O(n + m)
// SC: O(n + m)
void CountingSort(int *data, int first, int last) // [first, last)
{
	// Get the maximum value.
	int max_value = data[first];
	for(int index = first + 1; index < last; ++index)
	{
		if(max_value < data[index])
		{
			max_value = data[index];
		}
	}
	int count[max_value + 1]; // count[value] is the count of elements that are <= value.
	memset(count, 0, sizeof count);
	int temp[last]; // Temporary store the sorted elements.
	// Count the frequency of every value in data.
	for(int index = first; index < last; ++index)
	{
		++count[data[index]];
	}
	// Compute how many elements are <= value.
	for(int value = 1; value <= max_value; ++value)
	{
		count[value] += count[value - 1];
	}
	// 1. x elements <= value: place value in [x - 1].
	// 2. Place from back to front: keep stability.
	for(int index = last - 1; index >= first; --index)
	{
		temp[--count[data[index]]] = data[index];
	}
	// Copy the sorted elements into data.
	for(int index = first; index < last; ++index)
	{
		data[index] = temp[index];
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// n is the number of inputs, d is the number of digits, m is the max value in each digit.
// TC: Best = Average = Worst = O(d(n + m))
// SC: O(n + m)
void RadixSort(int *data, int first, int last)
{
	const int kMaxDigitValue = 9;
	const int kDigitValueNumber = kMaxDigitValue + 1;
	int digit_number[last], count[kDigitValueNumber], temp[last];
	const int kMaxDigitNumber = 10; // 10^10 < 2^31 < 10^11, thus has at most 10 digits.
	int divisor = 1;
	for(int digit = 1; digit <= kMaxDigitNumber; ++digit)
	{
		memset(count, 0, sizeof count);
		// Get each digit number from least to most significant digit.
		for(int index = first; index < last; ++index)
		{
			digit_number[index] = (data[index] / divisor) % kDigitValueNumber;
			++count[digit_number[index]];
		}
		for(int value = 1; value <= kMaxDigitValue; ++value)
		{
			count[value] += count[value - 1];
		}
		for(int index = last - 1; index >= first; --index)
		{
			// Sort according to digit_number, but store data.
			temp[--count[digit_number[index]]] = data[index];
		}
		for(int index = first; index < last; ++index)
		{
			data[index] = temp[index];
		}
		// Update divisor to get the next digit_number.
		divisor *= kDigitValueNumber;
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PrintData(int *data, int first, int last)
{
	for(int index = 0; index < last; ++index)
	{
		printf("%d ", data[index]);
	}
	printf("\n");
}
using SortFunction = void(*)(int*, int, int);
void Test(const char *name, SortFunction Sort)
{
	printf("----------%s----------\n", name);
	const int data_length = 10;
	int data[][data_length] =
	{
		{0,1,2,3,4,5,6,7,8,9},
		{9,8,7,6,5,4,3,2,1,0},
		{0,2,4,6,8,9,7,5,3,1}
	};
	int data_number = static_cast<int>(sizeof(data) / sizeof(data[0]));
	for(int data_index = 0; data_index < data_number; ++data_index)
	{
		Sort(data[data_index], 0, data_length);
		PrintData(data[data_index], 0, data_length);
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main()
{
	Test("SelectionSort", SelectionSort);
	Test("BubbleSort", BubbleSort);
	Test("InsertionSort", InsertionSort);
	Test("QuickSort", QuickSort);
	Test("MergeSort", MergeSort);
	Test("HeapSort", HeapSort);
	Test("CountingSort", CountingSort);
	Test("RadixSort", RadixSort);
}
