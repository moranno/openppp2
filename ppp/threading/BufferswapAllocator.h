#pragma once

#include <ppp/stdafx.h>
#include <ppp/threading/BufferblockAllocator.h>

namespace ppp
{
    namespace threading
    {
        class BufferswapAllocator final
        {
            typedef std::shared_ptr<BufferblockAllocator>               BufferblockAllocatorPtr;
            typedef ppp::list<BufferblockAllocatorPtr>                  BufferblockAllocatorList;
            typedef std::mutex                                          SynchronizedObject;
            typedef std::lock_guard<SynchronizedObject>                 SynchronizedObjectScope;

        public:
            /* FAT32 file-system maxsize ≈ 4GB ~ 2B */
            static constexpr uint64_t                                   MAX_MEMORY_BLOCK_SIZE = 1073741824; /* 4294967280 */

        public:
            BufferswapAllocator(const ppp::string& path, bool physical_memory) noexcept;
            BufferswapAllocator(const ppp::string& path, uint64_t memory_size, bool physical_memory) noexcept;
            virtual ~BufferswapAllocator() noexcept;

        public:
            void*                                                       Alloc(uint32_t allocated_size) noexcept;
            bool                                                        Free(const void* allocated_memory) noexcept;
            bool                                                        IsVaild() noexcept;
            bool                                                        IsPhysicalMemory() noexcept;
            std::shared_ptr<BufferblockAllocator>                       IsInBlock(const void* allocated_memory) noexcept;
            uint32_t                                                    GetPageSize() noexcept;
            uint64_t                                                    GetMemorySize() noexcept;
            uint64_t                                                    GetAvailableSize() noexcept;

        public:
            template <typename T>
            std::shared_ptr<T>                                          MakeArray(int length) noexcept {
                static_assert(sizeof(T) > 0, "can't make pointer to incomplete type");

                if (length < 1) {
                    return NULL;
                }

                T* memory = (T*)Alloc(length * sizeof(T));
                if (NULL == memory) {
                    if (physical_memory_) {
                        return make_shared_alloc<T>(length);
                    }
                    else {
                        return NULL;
                    }
                }

                return std::shared_ptr<T>(memory,
                    [this](void* allocated_memory) noexcept {
                        Free(allocated_memory);
                    });
            }

            template <typename T, typename... A>
            std::shared_ptr<T>                                          MakeObject(A&&... args) noexcept {
                static_assert(sizeof(T) > 0, "can't make pointer to incomplete type");

                void* memory = Alloc(sizeof(T));
                if (NULL == memory) {
                    if (physical_memory_) {
                        return make_shared_object<T>(std::forward<A&&>(args)...);
                    }
                    else {
                        return NULL;
                    }
                }

                T* m = new (memory) T(std::forward<A&&>(args)...);
                return std::shared_ptr<T>(m,
                    [this](T* p) noexcept {
                        if (NULL != p)
                        {
                            p->~T();
                            Free(p);
                        }
                    });
            }

            static std::shared_ptr<Byte>                                MakeByteArray(const std::shared_ptr<ppp::threading::BufferswapAllocator>& allocator, int datalen) noexcept {
                if (NULL != allocator) {
                    return allocator->MakeArray<Byte>(datalen);
                }
                else {
                    return make_shared_alloc<Byte>(datalen);
                }
            }

        private:
            SynchronizedObject                                          syncobj_;
            BufferblockAllocatorList                                    blocks_;
            int                                                         block_count_     = 0;
            uint64_t                                                    memory_size_     = 0;
            bool                                                        physical_memory_ = false;
        };
    }
}