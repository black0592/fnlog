/*
 *
 * MIT License
 *
 * Copyright (C) 2019 YaweiZhang <yawei.zhang@foxmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ===============================================================================
 *
 * (end of COPYRIGHT)
 */


 /*
  * AUTHORS:  YaweiZhang <yawei.zhang@foxmail.com>
  * VERSION:  0.0.1
  * PURPOSE:  FNLog is a cpp-based logging utility.
  * CREATION: 2019.4.20
  * RELEASED: 2019.6.27
  * QQGROUP:  524700770
  */


#pragma once
#ifndef _FN_LOG_LOG_H_
#define _FN_LOG_LOG_H_

#include "fn_data.h"
#include "fn_parse.h"
#include "fn_load.h"
#include "fn_core.h"

namespace FNLog
{

    Logger& GetDefaultLogger()
    {
        static std::once_flag once;
        static Logger logger;
        static GuardLogger gl(logger);
        std::call_once(once, [](Logger& logger) {InitLogger(logger);}, std::ref(logger));
        return logger;
    }

    int LoadAndStartDefaultLogger(const std::string& path)
    {
        int ret = InitFromYMALFile(path, GetDefaultLogger());
        if (ret != 0)
        {
            printf("init and load default logger error. ret:<%d>.", ret);
            return ret;
        }
        ret = AutoStartLogger(GetDefaultLogger());
        if (ret != 0)
        {
            printf("auto start default logger error. ret:<%d>.", ret);
            return ret;
        }
        return 0;
    }

    int FastStartDefaultLogger(const std::string& config_text)
    {
        int ret = InitFromYMAL(config_text, "", GetDefaultLogger());
        if (ret != 0)
        {
            printf("init default logger error. ret:<%d>.", ret);
            return ret;
        }
        ret = AutoStartLogger(GetDefaultLogger());
        if (ret != 0)
        {
            printf("auto start default logger error. ret:<%d>.", ret);
            return ret;
        }
        return 0;
    }

    int FastStartDefaultLogger()
    {
        static const std::string default_config_text =
R"----(
 # 默认配置  
 # 0通道为多线程文件输出和一个CLS筛选的屏显输出 
 - channel: 0
    sync: null
    -device: 0
        disable: false
        out_type: file
        file: "$PNAME_$YEAR$MON$DAY"
        rollback: 4
        limit_size: 100 m #only support M byte
    -device:1
        disable: false
        out_type: screen
        filter_level: info
)----";
        return FastStartDefaultLogger(default_config_text);
    }


    class LogStream
    {
    public:
        static const int MAX_CONTAINER_DEPTH = 5;
    public:
        explicit LogStream(LogStream&& ls) noexcept
        {
            logger_ = ls.logger_;
            log_data_ = ls.log_data_;
            ls.logger_ = nullptr;
            ls.log_data_ = nullptr;
        }

        explicit LogStream(Logger& logger, int channel_id, int filter_level, int filter_cls, 
            const char * const file_name, int file_name_len, int line,
            const char * const func_name, int func_name_len)
        {
            logger_ = nullptr;
            log_data_ = nullptr;
            if (CanPushLog(logger, channel_id, filter_level, filter_cls) != 0)
            {
                return;
            }
            logger_ = &logger;
            log_data_ = AllocLogData(logger, channel_id, filter_level, filter_cls);
            if (log_data_ == nullptr)
            {
                return;
            }

            write_buffer(" ", 1);
            if (file_name && file_name_len > 0)
            {
                int jump_bytes = short_path(file_name, file_name_len);
                write_buffer(file_name + jump_bytes, file_name_len - jump_bytes);
            }
            else
            {
                write_buffer("nofile", 6);
            }
            write_buffer(":<", 2);
            *this << (unsigned long long)line;
            write_buffer("> ", 2);
            
            if (func_name && func_name_len > 0)
            {
                write_buffer(func_name, func_name_len);
            }
            else
            {
                write_buffer("null", 4);
            }
            write_buffer(" ", 1);
        }
        
        ~LogStream()
        {
            if (log_data_) 
            {
                PushLog(*logger_, log_data_);
                log_data_ = nullptr;
            }
        }
        
        LogStream& set_filter_cls(int filter_cls) { if (log_data_) log_data_->filter_cls_ = filter_cls;  return *this; }
        LogStream& write_buffer(const char* src, int src_len)
        {
            if (log_data_ && src && src_len > 0)
            {
                log_data_->content_len_ 
                    += FNLog::write_buffer(log_data_->content_ + log_data_->content_len_, 
                        LogData::MAX_LOG_SIZE - log_data_->content_len_, src, src_len);
            }
            return *this;
        }
        LogStream& write_pointer(const void* ptr)
        {
            log_data_->content_len_ += FNLog::write_pointer(log_data_->content_ + log_data_->content_len_,
                LogData::MAX_LOG_SIZE - log_data_->content_len_, ptr);
            return *this;
        }

        LogStream& write_binary(const char* dst, int len)
        {
            if (!log_data_)
            {
                return *this;
            }
            write_buffer("\r\n\t[", sizeof("\r\n\t[")-1);
            for (int i = 0; i < (len / 32) + 1; i++)
            {
                write_buffer("\r\n\t[", sizeof("\r\n\t[") - 1);
                *this << (void*)(dst + (size_t)i * 32);
                write_buffer(": ", sizeof(": ") - 1);
                for (int j = i * 32; j < (i + 1) * 32 && j < len; j++)
                {
                    if (isprint((unsigned char)dst[j]))
                    {
                        write_buffer(" ", sizeof(" ") - 1);
                        write_buffer(dst + j, 1);
                        write_buffer(" ", sizeof(" ") - 1);
                    }
                    else
                    {
                        write_buffer(" . ", sizeof(" . ") - 1);
                    }
                }
                write_buffer("\r\n\t[", sizeof("\r\n\t[") - 1);
                write_pointer(dst + (size_t)i * 32);
                write_buffer(": ", sizeof(": ") - 1);
                for (int j = i * 32; j < (i + 1) * 32 && j < len; j++)
                {
                    log_data_->content_len_ += FNLog::write_integer<16, 2>(log_data_->content_ + log_data_->content_len_,
                        LogData::MAX_LOG_SIZE - log_data_->content_len_, (unsigned long long)(unsigned char)dst[j]);
                    write_buffer(" ", sizeof(" ") - 1);
                }
            }
            write_buffer("\r\n\t]\r\n\t", sizeof("\r\n\t]\r\n\t") - 1);
            return *this;
        }

        LogStream& operator <<(const char* cstr)
        {
            if (log_data_)
            {
                if (cstr)
                {
                    write_buffer(cstr, (int)strlen(cstr));
                }
                else
                {
                    write_buffer("null", 4);
                }
            }
            return *this;
        }
        LogStream& operator <<(const void* ptr)
        {
            if (log_data_)
            {
                if (ptr)
                {
                    write_pointer(ptr);
                }
                else
                {
                    write_buffer("null", 4);
                }
            }
            return *this;
        }
        
        LogStream& operator <<(std::nullptr_t) { return write_pointer(nullptr); }

        LogStream& operator << (char ch) { return write_buffer(&ch, 1);}
        LogStream & operator << (unsigned char ch) { return (*this << (unsigned long long)ch); }

        LogStream& operator << (bool val) { return (val ? write_buffer("true", 4) : write_buffer("false", 5)); }

        LogStream & operator << (short val) { return (*this << (long long)val); }
        LogStream & operator << (unsigned short val) { return (*this << (unsigned long long)val); }
        LogStream & operator << (int val) { return (*this << (long long)val); }
        LogStream & operator << (unsigned int val) { return (*this << (unsigned long long)val); }
        LogStream & operator << (long val) { return (*this << (long long)val); }
        LogStream & operator << (unsigned long val) { return (*this << (unsigned long long)val); }
        
        LogStream& operator << (long long integer)
        {
            if (log_data_)
            {
                log_data_->content_len_ += write_integer<10, 0>(log_data_->content_ + log_data_->content_len_, LogData::MAX_LOG_SIZE - log_data_->content_len_, (long long)integer);
            }
            return *this;
        }

        LogStream& operator << (unsigned long long integer)
        {
            if (log_data_)
            {
                log_data_->content_len_ += write_integer<10, 0>(log_data_->content_ + log_data_->content_len_, LogData::MAX_LOG_SIZE - log_data_->content_len_, (unsigned long long)integer);
            }
            return *this;
        }

        LogStream& operator << (float f)
        {
            if (log_data_)
            {
                log_data_->content_len_ += write_float(log_data_->content_ + log_data_->content_len_, LogData::MAX_LOG_SIZE - log_data_->content_len_, f);
            }
            return *this;
        }
        LogStream& operator << (double df)
        {
            if (log_data_)
            {
                log_data_->content_len_ += write_double(log_data_->content_ + log_data_->content_len_, LogData::MAX_LOG_SIZE - log_data_->content_len_, df);
            }
            return *this;
        }


        template<class _Ty1, class _Ty2>
        inline LogStream & operator <<(const std::pair<_Ty1, _Ty2> & val){ return *this << '<' <<val.first << ':' << val.second << '>'; }

        template<class Container>
        LogStream& write_container(const Container& container, const char* name, int len)
        {
            write_buffer(name, len);
            write_buffer("(", 1);
            *this << container.size();
            write_buffer(")[", 2);
            int inputCount = 0;
            for (auto iter = container.begin(); iter != container.end(); iter++)
            {
                if (inputCount >= MAX_CONTAINER_DEPTH)
                {
                    *this << "..., ";
                    break;
                }
                if(inputCount > 0)
                {
                    *this << ", ";
                }
                *this << *iter;
                inputCount++;
            }
            return *this << "]";
        }

        template<class _Elem, class _Alloc>
        LogStream & operator <<(const std::vector<_Elem, _Alloc> & val) { return write_container(val, "vector:", sizeof("vector:") - 1);}
        template<class _Elem, class _Alloc>
        LogStream & operator <<(const std::list<_Elem, _Alloc> & val) { return write_container(val, "list:", sizeof("list:") - 1);}
        template<class _Elem, class _Alloc>
        LogStream & operator <<(const std::deque<_Elem, _Alloc> & val) { return write_container(val, "deque:", sizeof("deque:") - 1);}
        template<class _Key, class _Tp, class _Compare, class _Allocator>
        LogStream & operator <<(const std::map<_Key, _Tp, _Compare, _Allocator> & val) { return write_container(val, "map:", sizeof("map:") - 1);}
        template<class _Key, class _Compare, class _Allocator>
        LogStream & operator <<(const std::set<_Key, _Compare, _Allocator> & val) { return write_container(val, "set:", sizeof("set:") - 1);}
        template<class _Key, class _Tp, class _Hash, class _Compare, class _Allocator>
        LogStream& operator <<(const std::unordered_map<_Key, _Tp, _Hash, _Compare, _Allocator>& val)
        {return write_container(val, "unordered_map:", sizeof("unordered_map:") - 1);}
        template<class _Key, class _Hash, class _Compare, class _Allocator>
        LogStream& operator <<(const std::unordered_set<_Key, _Hash, _Compare, _Allocator> & val)
        {return write_container(val, "unordered_set:", sizeof("unordered_set:") - 1);}
        template<class _Traits, class _Allocator>
        LogStream & operator <<(const std::basic_string<char, _Traits, _Allocator> & str) { return write_buffer(str.c_str(), (int)str.length());}
        
    public:
        LogData * log_data_ = nullptr;
        Logger* logger_ = nullptr;
    };
}



#define LOG_STREAM_IMPL(logger, channel, level, cls) \
FNLog::LogStream(logger, channel, level, cls, \
__FILE__, sizeof(__FILE__) - 1, \
__LINE__, __FUNCTION__, sizeof(__FUNCTION__) -1)

#define LOG_STREAM(channel_id, filter_level, cls_id) LOG_STREAM_IMPL(FNLog::GetDefaultLogger(), channel_id, filter_level, cls_id)

#define LOGCT(channel_id, cls_id) LOG_STREAM(channel_id, FNLog::LOG_LEVEL_TRACE, cls_id)
#define LOGCD(channel_id, cls_id) LOG_STREAM(channel_id, FNLog::LOG_LEVEL_DEBUG, cls_id)
#define LOGCI(channel_id, cls_id) LOG_STREAM(channel_id, FNLog::LOG_LEVEL_INFO,  cls_id)
#define LOGCW(channel_id, cls_id) LOG_STREAM(channel_id, FNLog::LOG_LEVEL_WARN,  cls_id)
#define LOGCE(channel_id, cls_id) LOG_STREAM(channel_id, FNLog::LOG_LEVEL_ERROR, cls_id)
#define LOGCA(channel_id, cls_id) LOG_STREAM(channel_id, FNLog::LOG_LEVEL_ALARM, cls_id)
#define LOGCF(channel_id, cls_id) LOG_STREAM(channel_id, FNLog::LOG_LEVEL_FATAL, cls_id)

#define LOGT() LOGCT(0,0)
#define LOGD() LOGCD(0,0)
#define LOGI() LOGCI(0,0)
#define LOGW() LOGCW(0,0)
#define LOGE() LOGCE(0,0)
#define LOGA() LOGCA(0,0)
#define LOGF() LOGCF(0,0)


#endif
