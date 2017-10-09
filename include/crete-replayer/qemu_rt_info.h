#ifndef BCT_REPLAY_H
#define BCT_REPLAY_H

#include <stdint.h>
#include <vector>
#include <string>
#include <map>

#include <boost/serialization/vector.hpp>
#include <boost/serialization/map.hpp>

#include <crete/trace_tag.h>
#include "crete/test_case.h"

using namespace std;

namespace klee {
class ObjectState;
class Executor;
class ExecutionState;
class Expr;
}

class QemuRuntimeInfo;

extern QemuRuntimeInfo *g_qemu_rt_Info;

extern uint64_t g_test_case_count;

/*****************************/
/* Functions for klee */
QemuRuntimeInfo* qemu_rt_info_initialize();
void qemu_rt_info_cleanup(QemuRuntimeInfo *qrt);
bool is_in_fork_blacklist(uint64_t tb_pc);

/*****************************/
/* structs and classes */

struct CPUStateElement{
    uint64_t m_offset;
    uint64_t m_size;
    string m_name;
    vector<uint8_t> m_data;

    CPUStateElement(uint64_t offset, uint64_t size, string name, vector<uint8_t> data)
    :m_offset(offset), m_size(size), m_name(name), m_data(data) {}

    // For serialization
    CPUStateElement()
    :m_offset(0), m_size(0), m_name(string()),
     m_data(vector<uint8_t>()) {}

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & m_offset;
        ar & m_size;
        ar & m_name;
        ar & m_data;
    }
};

struct QemuInterruptInfo {
    int m_intno;
    int m_is_int;
    int m_error_code;
    int m_next_eip_addend;

    QemuInterruptInfo(int intno, int is_int, int error_code, int next_eip_addend)
    :m_intno(intno), m_is_int(is_int),
     m_error_code(error_code), m_next_eip_addend(next_eip_addend) {}

    // For serialization
    QemuInterruptInfo()
        :m_intno(0), m_is_int(0), m_error_code(0), m_next_eip_addend(0) {}

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & m_intno;
        ar & m_is_int;
        ar & m_error_code;
        ar & m_next_eip_addend;
    }
};

// bool: valid table or not
// vector<>: contents
typedef pair<bool, vector<CPUStateElement> > cpuStateSyncTable_ty;

typedef pair<QemuInterruptInfo, bool> interruptState_ty;

class QemuRuntimeInfo {
private:
	// For Streaming Tracing
    uint64_t m_streamed_tb_count;
    uint64_t m_streamed_index;

    // For trace tag
    crete::creteTraceTag_ty m_trace_tag_explored;
    crete::creteTraceTag_ty m_trace_tag_semi_explored;
    crete::creteTraceTag_ty m_trace_tag_new;

    // Concolic Test Generation
    crete::TestCaseIssueIndex m_base_tc_issue_index;

    // For Debugging Purpose:
    // The CPUState after each interested TB being executed for cross checking on klee side
    vector<cpuStateSyncTable_ty> m_debug_cpuStateSyncTables;
	// Interrupt State Info dumped from QEMU
	vector< interruptState_ty > m_interruptStates;

public:
	QemuRuntimeInfo();
	~QemuRuntimeInfo();

	void cross_check_cpuState(klee::ExecutionState &state,
	        const klee::ObjectState *wos, uint64_t tb_index);

	// For Debugging
	QemuInterruptInfo get_qemuInterruptInfo(uint64_t tb_index);

	void update_qemu_CPUState(klee::ObjectState *wos,
	        uint64_t tb_index)
	__attribute__ ((deprecated));
	void verify_CpuSate_offset(string name, uint64_t offset, uint64_t size);

	//trace tag
	void check_trace_tag(uint64_t tt_tag_index, uint64_t tb_index,
	        vector<bool>& branch_taken, vector<bool>& current_node_br_taken_semi_explored,
	        bool& explored_node) const;
	void get_trace_tag_for_tc(uint64_t tt_tag_index,
	        crete::creteTraceTag_ty &tt_tag_for_tc,
	        vector<bool>& current_node_br_taken_semi_explored) const;
	uint64_t get_tt_node_br_num(uint64_t tt_tag_index) const;
	bool is_tt_node_explored(uint64_t tt_tag_index) const;

	// Concolic test generation
	crete::TestCaseIssueIndex get_base_tc_issue_index() const {return m_base_tc_issue_index;}

private:
	void read_streamed_trace();
	uint32_t read_debug_cpuSyncTables();
	void read_debug_cpuState_offsets();

	void init_interruptStates();
	//    uint32_t read_interruptStates();

	void init_trace_tag();

	// Debugging
	void init_debug_cpuOffsetTable();

public:
	// <offset, <name, size> >
	map<uint64_t, pair<string, uint64_t> >m_debug_cpuOffsetTable;

    map<string, pair<uint64_t, uint64_t> > m_debug_cpuState_offsets;
};
#endif
