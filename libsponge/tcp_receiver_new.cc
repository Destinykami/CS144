#include "tcp_receiver.hh"
// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.
using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) 
{
    /*
    defined in tcp_receiver.hh:
    bool _syn_flag = false;
    bool _fin_flag = false;
    size_t _base = 0; 
    size_t _isn = 0;  //第一次握手时的初始化序列号
    */
    bool result = false; //用于指示处理传入的TCP分段后的结果
    static size_t abs_seqno = 0;
    size_t length;
    if (seg.header().syn) //TCP段的syn标识为1表示建立连接
    {
        if(_syn_flag==1)
            return false;
        else
        {
            _syn_flag=1;
            result=true;//该字段被正确处理
            _isn=seg.header().seqno.raw_value();
            abs_seqno=1;
            _base=1;
            length=seg.length_in_sequence_space()-1;
            if(length==0)
                return true;
        }
    }
    else if(!_syn_flag)
        return false;
    else
    {
        abs_seqno = unwrap(WrappingInt32(seg.header().seqno.raw_value()), WrappingInt32(_isn), abs_seqno);
        length = seg.length_in_sequence_space();
    }

    if(seg.header().fin)
    {
        if (_fin_flag) 
            return false;
        else
        {
            _fin_flag = true;
            result = true;
        }
    }
    // not FIN and not one size's SYN, check border
    else if (seg.length_in_sequence_space() == 0 && abs_seqno == _base) 
    {
        return true;
    } 
    else if (abs_seqno >= _base + window_size() || abs_seqno + length <= _base) 
    {
        if (!result)
            return false;
    }
    _reassembler.push_substring(seg.payload().copy(), abs_seqno - 1, seg.header().fin);
    _base = _reassembler.head_index() + 1;
    if (_reassembler.input_ended())  // FIN be count as one byte
        _base++;
    return true;
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (_base > 0)
        return WrappingInt32(wrap(_base, WrappingInt32(_isn)));
    else
        return std::nullopt; 
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
