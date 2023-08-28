#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <iterator>
#include <cassert>

template <typename T>
class CappedArray
{
public:
  ~CappedArray()
  {
    if (mAllocated)
      free(mElements);
  }

  CappedArray()
  {
  }

  CappedArray(CappedArray<T> &&other)
  {
    other.mAllocated = false;
    mElements = other.mElements;
    mSize = other.mSize;
    mCapacity = other.mCapacity;
    mAllocated = other.mAllocated;
  }

  CappedArray<T> &operator=(CappedArray<T> &&other)
  {
    other.mAllocated = false;
    mElements = other.mElements;
    mSize = other.mSize;
    mCapacity = other.mCapacity;
    mAllocated = other.mAllocated;
    return *this;
  }

  inline void alloc(size_t max)
  {
    mElements = (T *)malloc(sizeof(T) * max);
    mSize = 0;
    mCapacity = max;

    mAllocated = true;
  }

  inline void setPtr(void *ptr, size_t max)
  {
    mElements = (T *)ptr;
    mSize = 0;
    mCapacity = max;

    mAllocated = false;
  }

  inline size_t push(const T &elem)
  {
    assert(mSize < mCapacity);

    size_t idx = mSize;
    mElements[mSize++] = elem;
    return idx;
  }

  inline void clear() { mSize = 0; }
  inline size_t size() const { return mSize; }
  inline T *data() { return mElements; }
  inline size_t capacity() const { return mCapacity; }
  inline T &operator[](uint32_t i) { return mElements[i]; }
  inline const T &operator[](uint32_t i) const { return mElements[i]; }

public:
  class iterator 
  {
  public:
    using value_type = T;
    using reference = T &;
    using self_type = iterator;
    using pointer = T *;
    using difference_type = int;
    using iterator_category = std::forward_iterator_tag;

    iterator(CappedArray<T> *container, uint32_t index)
      : mContainer(container), mIndex(index)
    {
    }

    self_type operator++() 
    {
      self_type current = *this;
      ++mIndex;
      return current;
    }

    self_type operator++(int) 
    {
      ++mIndex;
      return *this;
    }

    reference operator*() {return mContainer->mData[mIndex];}
    pointer operator->() {return &mContainer->mData[mIndex];}
    bool operator==(const self_type &rhs) {return this->mIndex == rhs.mIndex;}
    bool operator!=(const self_type &rhs) {return this->mIndex != rhs.mIndex;}

  private:
    CappedArray<T> *mContainer;
    uint32_t mIndex;
  };

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, mSize); }

private:
  T *mElements;
  size_t mSize;
  size_t mCapacity;

  bool mAllocated;
};
