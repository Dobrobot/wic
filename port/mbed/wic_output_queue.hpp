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

#ifndef WIC_OUTPUT_QUEUE_HPP
#define WIC_OUTPUT_QUEUE_HPP

#include "mbed.h"
#include "wic.h"
#include "wic_buffer.hpp"

namespace WIC {

    class OutputQueueBase {

        public:

            virtual BufferBase *alloc(enum wic_frame_type type, size_t min_size) = 0;
            virtual void put(BufferBase *buf) = 0;
            virtual void free(BufferBase *buf) = 0;
            virtual BufferBase *get() = 0;
            virtual void clear() = 0;
    };

    template<size_t MAX_SIZE>
    class OutputQueue : public OutputQueueBase {

        protected:

            Buffer<MAX_SIZE> user;
            Buffer<127> close;
            Buffer<2> ping;
            Buffer<127> pong;

            EventFlags flags;
            
            rtos::Queue<BufferBase, 4> queue;

            BufferBase *type_to_buf(enum wic_frame_type type)
            {
                BufferBase *retval = nullptr;

                switch(type){
                case WIC_FRAME_TYPE_HTTP:
                case WIC_FRAME_TYPE_USER:
                    retval = &user;
                    break;
                case WIC_FRAME_TYPE_CLOSE:
                case WIC_FRAME_TYPE_CLOSE_RESPONSE:
                    retval = &close;
                    break;
                case WIC_FRAME_TYPE_PONG:
                    retval = &pong;
                    break;
                case WIC_FRAME_TYPE_PING:
                    retval = &ping;
                    break;
                }

                return retval;
            }
                
        public:

            OutputQueue() :
                user(1U, 0U),
                close(2U, 2U),
                ping(4U, 0U),
                pong(8U, 1U)
            {}

            BufferBase *alloc(enum wic_frame_type type, size_t min_size)
            {
                BufferBase *retval = nullptr;         
                BufferBase *buf = type_to_buf(type);

                if(min_size == 0U || buf->max >= min_size){

                    if((buf->mask & flags.get()) == 0U){

                        flags.set(buf->mask);
                        retval = buf;
                    }
                }

                return retval;
            }

            void put(BufferBase *buf)
            {
                if(buf->size > 0U){

                    queue.put(buf, buf->priority);
                }
                else{

                    free(buf);
                }                
            }

            void free(BufferBase *buf)
            {
                flags.clear(buf->mask);                                
            }
            
            BufferBase *get()
            {
                osEvent evt = queue.get();

                if (evt.status == osEventMessage) {

                    return static_cast<BufferBase *>(evt.value.p);    
                }
                else{

                    return nullptr;
                }
            }

            void clear()
            {
                BufferBase *buf;
                osEvent evt;

                do{

                    evt = queue.get(0);
                    
                    if(evt.status == osEventMessage){

                        buf = static_cast<BufferBase *>(evt.value.p);

                        free(buf);
                    }
                }
                while(evt.status == osEventMessage);                
            }
    };
}

#endif
