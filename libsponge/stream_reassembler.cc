#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;
//构建一个流重组器
StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {
    _stream.resize(capacity, 0);
    _dirty.resize(capacity, 0);
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) 
{
    //收到的信息编号大于期望编号加上接受窗口的长度 或者收到的信息编号上信息长度小于期望编号 跳过
    if (index >= _capacity + _next_index || index + data.length() < _next_index)
        return;

    size_t actual_index = index;
    size_t data_index = 0;
    //这一段信息包含了在期望的序号后的一段序列   从_next_index - index 开始是需要的序列
    //构建新序列  抛弃前面已接收过的字节
    if (index < _next_index) 
    {
        actual_index = _next_index;
        data_index += _next_index - index;
    }
    //类似操作系统将物理地址映射到虚拟地址的操作  
    size_t start_index = _next_index % _capacity;//接收窗口的接收起点
    size_t loop_index = actual_index % _capacity;//向窗口内写入数据的位置
    if (data.empty()) //内容为空
    {
        if (eof) 
        {
            _is_eof = true;
        }
    }
    //脏位为0说明窗口该数据未曾写入数据
    for (size_t i = loop_index, j = data_index; j < data.size(); i = next(i), j++) 
    {
        if (_dirty[i]==0) 
        {
            _stream[i] = data[j];
            _unassembly++;
            _dirty[i] = 1;
        }
        if (j == data.size()-1) 
        {
          if (eof) 
          {
            _is_eof = true;
          }
        }
        if (next(i) == start_index) //已经把整个窗口都写过了，剩余数据舍弃
            break;
    }
    //从窗口中按序提取数据
    string send_str="";
    for (size_t i = start_index; _dirty[i]; i = next(i)) 
    {
        send_str.push_back(_stream[i]);
        if (next(i) == start_index)
            break;
    }
    //写入字节流_output   ByteStream _output;
    if (!send_str.empty()) 
    {
        size_t write_num = _output.write(send_str);  //write的返回值为写入的数据长度
        for (size_t i = start_index, j = 0; j < write_num; i = next(i), ++j) 
        {
            _dirty[i] = 0;
        }
        _next_index += write_num;
        _unassembly -= write_num;
    }

    if (_is_eof && empty()) //输入结束且没有未排序的字节
    {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembly; }

bool StreamReassembler::empty() const { return _unassembly==0; }
