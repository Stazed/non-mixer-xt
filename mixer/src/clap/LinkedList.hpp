/*
 * High-level, templated, C++ doubly-linked list
 * Copyright (C) 2013-2022 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the doc/GPL.txt file.
 */

#ifndef LINKED_LIST_HPP_INCLUDED
#define LINKED_LIST_HPP_INCLUDED

#ifdef CLAP_SUPPORT

#include "../x11/XTUtils.H"

// -----------------------------------------------------------------------
// Define list_entry and list_entry_const

#ifndef offsetof
# define offsetof(TYPE, MEMBER) ((std::size_t) &((TYPE*)nullptr)->MEMBER)
#endif

#if (defined(__GNUC__) || defined(__clang__)) && ! defined(__STRICT_ANSI__)
# define list_entry(ptr, type, member) ({                     \
    typeof( ((type*)nullptr)->member ) *__mptr = (ptr);       \
    (type*)( (char*)__mptr - offsetof(type, member) );})
# define list_entry_const(ptr, type, member) ({               \
    const typeof( ((type*)nullptr)->member ) *__mptr = (ptr); \
    (const type*)( (const char*)__mptr - offsetof(type, member) );})
#else
# define list_entry(ptr, type, member)       \
    ((type*)((char*)(ptr)-offsetof(type, member)))
# define list_entry_const(ptr, type, member) \
    ((const type*)((const char*)(ptr)-offsetof(type, member)))
#endif

// -----------------------------------------------------------------------
// Abstract Linked List class
// _allocate() and _deallocate are virtual calls provided by subclasses

// NOTE: this class is meant for non-polymorphic data types only!

template<typename T>
class AbstractLinkedList
{
protected:
    struct ListHead {
        ListHead* next;
        ListHead* prev;
    };

    struct Data {
        T value;
        ListHead siblings;
    };

    AbstractLinkedList() noexcept
        : kDataSize(sizeof(Data)),
#ifdef CARLA_PROPER_CPP11_SUPPORT
          fQueue({&fQueue, &fQueue}),
#endif
          fCount(0)
    {
#ifndef CARLA_PROPER_CPP11_SUPPORT
        fQueue.next = &fQueue;
        fQueue.prev = &fQueue;
#endif
    }

public:
    virtual ~AbstractLinkedList() noexcept
    {
        CARLA_SAFE_ASSERT(fCount == 0);
    }

    class Itenerator {
    public:
        explicit Itenerator(const ListHead& queue) noexcept
            : fEntry(queue.next),
              fEntry2(fEntry->next),
              kQueue(queue)
        {
            CARLA_SAFE_ASSERT(fEntry != nullptr);
            CARLA_SAFE_ASSERT(fEntry2 != nullptr);
        }

        bool valid() const noexcept
        {
            return (fEntry != nullptr && fEntry != &kQueue);
        }

        void next() noexcept
        {
            fEntry  = fEntry2;
            fEntry2 = (fEntry != nullptr) ? fEntry->next : nullptr;
        }

        T& getValue(T& fallback) const noexcept
        {
            Data* const data(list_entry(fEntry, Data, siblings));
            CARLA_SAFE_ASSERT_RETURN(data != nullptr, fallback);

            return data->value;
        }

        const T& getValue(const T& fallback) const noexcept
        {
            const Data* const data(list_entry_const(fEntry, Data, siblings));
            CARLA_SAFE_ASSERT_RETURN(data != nullptr, fallback);

            return data->value;
        }

        void setValue(const T& value) noexcept
        {
            Data* const data(list_entry(fEntry, Data, siblings));
            CARLA_SAFE_ASSERT_RETURN(data != nullptr,);

            data->value = value;
        }

    private:
        ListHead* fEntry;
        ListHead* fEntry2;
        const ListHead& kQueue;

        friend class AbstractLinkedList;
    };

    class AutoItenerator {
    public:
        explicit AutoItenerator(const ListHead* entry) noexcept
            : fEntry(entry),
              fEntry2(entry != nullptr ? entry->next : nullptr)
        {
            CARLA_SAFE_ASSERT(fEntry != nullptr);
            CARLA_SAFE_ASSERT(fEntry2 != nullptr);
        }

        bool operator!=(const AutoItenerator& it) const noexcept
        {
            CARLA_SAFE_ASSERT_RETURN(fEntry != nullptr, false);
            CARLA_SAFE_ASSERT_RETURN(it.fEntry != nullptr, false);

            return fEntry != it.fEntry;
        }

        AutoItenerator& operator++() noexcept
        {
            fEntry  = fEntry2;
            fEntry2 = (fEntry != nullptr) ? fEntry->next : nullptr;
            return *this;
        }

#if 0
        T& operator*() noexcept
        {
            static T& fallback(_getFallback());

            Data* const data(list_entry(fEntry, Data, siblings));
            CARLA_SAFE_ASSERT_RETURN(data != nullptr, fallback);

            return data->value;
        }
#endif

        const T& operator*() const noexcept
        {
            static const T& fallback(_getFallback());

            const Data* const data(list_entry_const(fEntry, Data, siblings));
            CARLA_SAFE_ASSERT_RETURN(data != nullptr, fallback);

            return data->value;
        }

    private:
        const ListHead* fEntry;
        const ListHead* fEntry2;

        static T& _getFallback()
        {
            static T data;
            carla_zeroStruct(data);
            return data;
        }
    };

    Itenerator begin2() const noexcept
    {
        return Itenerator(fQueue);
    }

    AutoItenerator begin() const noexcept
    {
        return AutoItenerator(fQueue.next);
    }

    AutoItenerator end() const noexcept
    {
        return AutoItenerator(&fQueue);
    }

    void clear() noexcept
    {
        if (fCount == 0)
            return;

        for (ListHead *entry = fQueue.next, *entry2 = entry->next; entry != &fQueue; entry = entry2, entry2 = entry->next)
        {
            Data* const data(list_entry(entry, Data, siblings));
            CARLA_SAFE_ASSERT_CONTINUE(data != nullptr);

            _deallocate(data);
        }

        _init();
    }

    inline std::size_t count() const noexcept
    {
        return fCount;
    }

    inline bool isEmpty() const noexcept
    {
        return fCount == 0;
    }

    inline bool isNotEmpty() const noexcept
    {
        return fCount != 0;
    }

    bool append(const T& value) noexcept
    {
        return _add(value, true, &fQueue);
    }

    bool appendAt(const T& value, const Itenerator& it) noexcept
    {
        return _add(value, true, it.fEntry->next);
    }

    bool insert(const T& value) noexcept
    {
        return _add(value, false, &fQueue);
    }

    bool insertAt(const T& value, const Itenerator& it) noexcept
    {
        return _add(value, false, it.fEntry->prev);
    }

    // NOTE: do not use this function unless strictly needed. it can be very expensive if the list is big
    const T& getAt(const std::size_t index, const T& fallback) const noexcept
    {
        CARLA_SAFE_ASSERT_UINT2_RETURN(fCount > 0 && index < fCount, index, fCount, fallback);

        std::size_t i = 0;
        ListHead* entry = fQueue.next;

        for (; i++ != index; entry = entry->next) {}

        return _get(entry, fallback);
    }

    T getFirst(T& fallback, const bool removeObj) noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(fCount > 0, fallback);

        return _get(fQueue.next, fallback, removeObj);
    }

    T& getFirst(T& fallback) const noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(fCount > 0, fallback);

        return _get(fQueue.next, fallback);
    }

    const T& getFirst(const T& fallback) const noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(fCount > 0, fallback);

        return _get(fQueue.next, fallback);
    }

    T getLast(T& fallback, const bool removeObj) noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(fCount > 0, fallback);

        return _get(fQueue.prev, fallback, removeObj);
    }

    T& getLast(T& fallback) const noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(fCount > 0, fallback);

        return _get(fQueue.prev, fallback);
    }

    const T& getLast(const T& fallback) const noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(fCount > 0, fallback);

        return _get(fQueue.prev, fallback);
    }

    void remove(Itenerator& it) noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(it.fEntry != nullptr,);

        Data* const data(list_entry(it.fEntry, Data, siblings));
        CARLA_SAFE_ASSERT_RETURN(data != nullptr,);

        _delete(it.fEntry, data);
    }

    bool removeOne(const T& value) noexcept
    {
        for (ListHead *entry = fQueue.next, *entry2 = entry->next; entry != &fQueue; entry = entry2, entry2 = entry->next)
        {
            Data* const data = list_entry(entry, Data, siblings);
            CARLA_SAFE_ASSERT_CONTINUE(data != nullptr);

            if (data->value != value)
                continue;

            _delete(entry, data);

            return true;
        }

        return false;
    }

    void removeAll(const T& value) noexcept
    {
        for (ListHead *entry = fQueue.next, *entry2 = entry->next; entry != &fQueue; entry = entry2, entry2 = entry->next)
        {
            Data* const data = list_entry(entry, Data, siblings);
            CARLA_SAFE_ASSERT_CONTINUE(data != nullptr);

            if (data->value != value)
                continue;

            _delete(entry, data);
        }
    }

    // move data to a new list, and clear ourselves
    virtual bool moveTo(AbstractLinkedList<T>& list, const bool inTail = true) noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(fCount > 0, false);

        if (inTail)
            __list_splice_tail(&fQueue, &list.fQueue);
        else
            __list_splice(&fQueue, &list.fQueue);

        //! @a list gets our items
        list.fCount += fCount;

        //! and we get nothing
        _init();

        return true;
    }

protected:
    const std::size_t kDataSize;

    ListHead    fQueue;
    std::size_t fCount;

    virtual Data* _allocate() noexcept = 0;
    virtual void  _deallocate(Data* const dataPtr) noexcept = 0;

private:
    void _init() noexcept
    {
        fCount = 0;
        fQueue.next = &fQueue;
        fQueue.prev = &fQueue;
    }

    bool _add(const T& value, const bool inTail, ListHead* const queue) noexcept
    {
        if (Data* const data = _allocate())
            return _add_internal(data, value, inTail, queue);
        return false;
    }

    bool _add_internal(Data* const data, const T& value, const bool inTail, ListHead* const queue) noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(data        != nullptr, false);
        CARLA_SAFE_ASSERT_RETURN(queue       != nullptr, false);
        CARLA_SAFE_ASSERT_RETURN(queue->prev != nullptr, false);
        CARLA_SAFE_ASSERT_RETURN(queue->next != nullptr, false);

        data->value = value;

        ListHead* const siblings(&data->siblings);

        if (inTail)
        {
            siblings->prev = queue->prev;
            siblings->next = queue;

            queue->prev->next = siblings;
            queue->prev       = siblings;
        }
        else
        {
            siblings->prev = queue;
            siblings->next = queue->next;

            queue->next->prev = siblings;
            queue->next       = siblings;
        }

        ++fCount;
        return true;
    }

    void _delete(ListHead* const entry, Data* const data) noexcept
    {
        CARLA_SAFE_ASSERT_RETURN(entry       != nullptr,);
        CARLA_SAFE_ASSERT_RETURN(entry->prev != nullptr,);
        CARLA_SAFE_ASSERT_RETURN(entry->next != nullptr,);

        --fCount;

        entry->next->prev = entry->prev;
        entry->prev->next = entry->next;

        entry->next = nullptr;
        entry->prev = nullptr;

        _deallocate(data);
    }

    T _get(ListHead* const entry, T& fallback, const bool removeObj) noexcept
    {
        Data* const data(list_entry(entry, Data, siblings));
        CARLA_SAFE_ASSERT_RETURN(data != nullptr, fallback);

        if (! removeObj)
            return data->value;

        const T value(data->value);

        _delete(entry, data);

        return value;
    }

    T& _get(ListHead* const entry, T& fallback) const noexcept
    {
        Data* const data(list_entry(entry, Data, siblings));
        CARLA_SAFE_ASSERT_RETURN(data != nullptr, fallback);

        return data->value;
    }

    const T& _get(const ListHead* const entry, const T& fallback) const noexcept
    {
        const Data* const data(list_entry_const(entry, Data, siblings));
        CARLA_SAFE_ASSERT_RETURN(data != nullptr, fallback);

        return data->value;
    }

    static void __list_splice(ListHead* const list, ListHead* const head) noexcept
    {
        ListHead* const first = list->next;
        ListHead* const last = list->prev;
        ListHead* const at = head->next;

        first->prev = head;
        head->next  = first;

        last->next = at;
        at->prev   = last;
    }

    static void __list_splice_tail(ListHead* const list, ListHead* const head) noexcept
    {
        ListHead* const first = list->next;
        ListHead* const last = list->prev;
        ListHead* const at = head->prev;

        first->prev = at;
        at->next    = first;

        last->next = head;
        head->prev = last;
    }

 //   template<typename> friend class RtLinkedList;

 //   CARLA_PREVENT_VIRTUAL_HEAP_ALLOCATION
  //  CARLA_DECLARE_NON_COPYABLE(AbstractLinkedList)
};

// -----------------------------------------------------------------------
// LinkedList

template<typename T>
class LinkedList : public AbstractLinkedList<T>
{
public:
    LinkedList() noexcept {}

protected:
    typename AbstractLinkedList<T>::Data* _allocate() noexcept override
    {
        return (typename AbstractLinkedList<T>::Data*)std::malloc(this->kDataSize);
    }

    void _deallocate(typename AbstractLinkedList<T>::Data* const dataPtr) noexcept override
    {
        std::free(dataPtr);
    }

//    CARLA_PREVENT_VIRTUAL_HEAP_ALLOCATION
//    CARLA_DECLARE_NON_COPYABLE(LinkedList)
};

// -----------------------------------------------------------------------

#endif  // CLAP_SUPPORT

#endif // LINKED_LIST_HPP_INCLUDED
