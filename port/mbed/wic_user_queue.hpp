/* Copyright (c) 2020 Cameron Harper
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * */

#ifndef WIC_USER_QUEUE_HPP
#define WIC_USER_QUEUE_HPP

#include "mbed.h"
#include "wic_buffer.hpp"

namespace WIC {

    class UserQueueBase {

        public:

            virtual BufferBase *alloc() = 0;
            virtual void free(BufferBase **buf) = 0;

            virtual void put(BufferBase **buf) = 0;            
            virtual osEvent get(uint32_t timeout) = 0;         
    };

    template<size_t SIZE, size_t DEPTH>
    class UserQueue : public UserQueueBase {

        protected:

            rtos::Mail<Buffer<SIZE>, DEPTH> mail;

        public:

            BufferBase *alloc()
            {
                return static_cast<BufferBase *>(mail.alloc());
            }

            void free(BufferBase **buf)
            {
                BufferBase *ptr = *buf;

                *buf = nullptr;

                if(ptr){
                
                    mail.free(static_cast<Buffer<SIZE> *>(ptr));
                }
            }

            osEvent get(uint32_t timeout)
            {
                return mail.get(timeout);                
            }

            void put(BufferBase **buf)
            {
                BufferBase *ptr = *buf;

                *buf = nullptr;

                if(ptr){

                    mail.put(static_cast<Buffer<SIZE> *>(ptr));
                }
            }
            
    };
}

#endif
