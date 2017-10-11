#include "crete-replayer/qemu_rt_info.h"
#include "../Core/Memory.h"
#include "klee/ExecutionState.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <assert.h>
#include <iomanip>

#include <boost/unordered_set.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <sstream>

QemuRuntimeInfo *g_qemu_rt_Info = 0;

QemuRuntimeInfo::QemuRuntimeInfo()
{
    m_streamed_tb_count = 0;
    m_streamed_index = 0;
    m_base_tc_issue_index = 0;

    // to-be-streamed
    init_interruptStates();

    // not-streamed
    init_trace_tag();

    CRETE_CK(read_debug_cpuState_offsets(););
}

QemuRuntimeInfo::~QemuRuntimeInfo()
{
}

static void check_cpu_state(klee::ExecutionState &state, const klee::ObjectState *os_current_cpu_state,
                            const vector<CPUStateElement>& correct_cpu_state, uint64_t tb_index)
{
    bool cross_check_passed = true;

    for(vector<CPUStateElement>::const_iterator it = correct_cpu_state.begin();
            it != correct_cpu_state.end(); ++it) {

        if(it->m_name.find("debug_cc_src") != string::npos)
            continue;

        vector<uint8_t> current_value;
        for(uint64_t i = 0; i < it->m_size; ++i) {
            klee::ref<klee::Expr> ref_current_value_byte;
            uint8_t current_value_byte;

            ref_current_value_byte = os_current_cpu_state->read8(it->m_offset + i);
            if(!isa<klee::ConstantExpr>(ref_current_value_byte)) {
                ref_current_value_byte = state.getConcreteExpr(ref_current_value_byte);
                assert(isa<klee::ConstantExpr>(ref_current_value_byte));
            }
            current_value_byte = (uint8_t)llvm::cast<klee::ConstantExpr>(
                    ref_current_value_byte)->getZExtValue(8);

            current_value.push_back(current_value_byte);
        }

        vector<uint8_t> correct_value = it->m_data;
        assert(correct_value.size() == it->m_size);
        bool cross_check_passed_current = true;
        for(uint64_t i = 0; i < it->m_size; ++i) {
            if(current_value[i] != correct_value[i]) {
                cross_check_passed_current = false;
                break;
            }
        }

        cross_check_passed = cross_check_passed && cross_check_passed_current;

        if(!cross_check_passed_current) {
            fprintf(stderr, "[CRETE ERROR] check_cpu_state() failed "
                    "after tb-%lu on %s\n", tb_index, it->m_name.c_str());

            cerr << "current value: [";
            for(uint64_t i = 0; i < it->m_size; ++i) {
                cerr << " 0x" << hex << (uint32_t)current_value[i];
            }
            cerr << "]\n";

            cerr << "correct value: [";
            for(uint64_t i = 0; i < it->m_size; ++i) {
                cerr << " 0x" << hex << (uint32_t)correct_value[i];
            }
            cerr << "]\n";

//            concretize_incorrect_cpu_element(os, *it);
        }
    }

    if(!cross_check_passed){
        state.print_stack();
        assert(0);
    }
}

//Get the information of concolic variables from file "dump_mo_symbolics" and "concrete_inputs.bin"
void QemuRuntimeInfo::init_trace_tag()
{
    using namespace crete;

    ifstream inputs("concrete_inputs.bin", ios_base::in | ios_base::binary);
    assert(inputs && "failed to open concrete_inputs file!");
    const TestCase tc = read_serialized(inputs);

    m_base_tc_issue_index = tc.get_issue_index();

    m_trace_tag_explored = tc.get_traceTag_explored_nodes();
    m_trace_tag_semi_explored = tc.get_traceTag_semi_explored_node();
    m_trace_tag_new = tc.get_traceTag_new_nodes();

    CRETE_DBG_TT(
    fprintf(stderr, "init_concolics():\n");
    fprintf(stderr, "m_trace_tag_explored:\n");
    crete::debug::print_trace_tag(m_trace_tag_explored);
    fprintf(stderr, "m_trace_tag_semi_explored:\n");
    crete::debug::print_trace_tag(m_trace_tag_semi_explored);
    fprintf(stderr, "m_trace_tag_new:\n");
    crete::debug::print_trace_tag(m_trace_tag_new);
    );
}

void QemuRuntimeInfo::init_interruptStates()
{
    ifstream i_sm("dump_qemu_interrupt_info.bin", ios_base::binary);
    assert(i_sm && "open file failed: dump_qemu_interrupt_info.bin\n");

    boost::archive::binary_iarchive ia(i_sm);
    ia >> m_interruptStates;
}

void QemuRuntimeInfo::read_streamed_trace()
{
    uint32_t read_amt_dbg_cst = read_debug_cpuSyncTables();

    m_streamed_tb_count += read_amt_dbg_cst;
    ++m_streamed_index;
}

uint32_t QemuRuntimeInfo::read_debug_cpuSyncTables()
{
    stringstream ss;
    ss << "dump_debug_sync_cpu_states." << m_streamed_index << ".bin";
    ifstream i_sm(ss.str().c_str(), ios_base::binary);
    if(!i_sm.good()) {
        cerr << "[Crete Error] can't find file " << ss.str() << endl;
        assert(0);
    }

    boost::archive::binary_iarchive ia(i_sm);
    m_debug_cpuStateSyncTables.clear();
    ia >> m_debug_cpuStateSyncTables;

    CRETE_CK(
    if(m_debug_cpuOffsetTable.empty()){
        init_debug_cpuOffsetTable();
    });

    return m_debug_cpuStateSyncTables.size();
}

void QemuRuntimeInfo::read_debug_cpuState_offsets()
{
    ifstream i_sm("dump_debug_cpuState_offsets.bin", ios_base::binary);
    if(!i_sm.good()) {
        cerr << "[Crete Error] can't find file dump_debug_cpuState_offsets.bin\n";
        assert(0);
    }

    boost::archive::binary_iarchive ia(i_sm);
    assert(m_debug_cpuState_offsets.empty());
    m_debug_cpuState_offsets.clear();
    ia >> m_debug_cpuState_offsets;
}

QemuInterruptInfo QemuRuntimeInfo::get_qemuInterruptInfo(uint64_t tb_index)
{
	return m_interruptStates[tb_index].first;
}

void QemuRuntimeInfo::update_qemu_CPUState(klee::ObjectState *wos, uint64_t tb_index)
{
	assert(0);
}

static void concretize_incorrect_cpu_element(klee::ObjectState *cpu_os,
        const CPUStateElement &correct_cpu_element) {
    cerr << "[CRETE Warning] concretize_incorrect_cpu_element(): "
            << correct_cpu_element.m_name << endl;

    assert(correct_cpu_element.m_data.size() == correct_cpu_element.m_size);
    cpu_os->write_n(correct_cpu_element.m_offset,correct_cpu_element.m_data);
}

void QemuRuntimeInfo::cross_check_cpuState(klee::ExecutionState &state,
        const klee::ObjectState *os, uint64_t tb_index_input) {
    if(tb_index_input == 0)
        return;

    CRETE_DBG(cerr << "\ncross_check_cpuState() being called after tb-" << dec << tb_index_input - 1 << endl;);

    // Adjust the tb_index: cross_check_cpuState() is being called
    // before the execution of the next interested tb
    uint64_t tb_index = tb_index_input - 1;
    assert(tb_index < m_streamed_tb_count);

    //TODO: xxx skip the check on the last tb of streamed trace, as it is not available
    if( tb_index < (m_streamed_tb_count - m_debug_cpuStateSyncTables.size()))
        return;

    uint64_t adjusted_tb_index = tb_index - (m_streamed_tb_count - m_debug_cpuStateSyncTables.size());

    assert(m_debug_cpuStateSyncTables[adjusted_tb_index].first);
    const vector<CPUStateElement>& correct_cpuStates =
            m_debug_cpuStateSyncTables[adjusted_tb_index].second;

    check_cpu_state(state, os, correct_cpuStates, tb_index);

    CRETE_DBG(cerr << "-------------------------------------------------------\n\n";);
}

void QemuRuntimeInfo::init_debug_cpuOffsetTable()
{
    assert(!m_debug_cpuStateSyncTables.empty());
    const vector<CPUStateElement> cpuState = m_debug_cpuStateSyncTables[0].second;
    for(vector<CPUStateElement>::const_iterator it = cpuState.begin();
            it != cpuState.end(); ++ it) {
        m_debug_cpuOffsetTable.insert(make_pair(it->m_offset,
                make_pair(it->m_name, it->m_size)));
    }
}

void QemuRuntimeInfo::verify_CpuSate_offset(string name, uint64_t offset, uint64_t size)
{
    assert(!m_debug_cpuState_offsets.empty());
    map<string, pair<uint64_t, uint64_t> >::iterator it = m_debug_cpuState_offsets.find(name);

    if(it == m_debug_cpuState_offsets.end())
    {
//        cerr << "[CRETE Warning] verify_CpuSate_offset() can't find " << name << endl;
        return;
    }

    if( (it->second.first != offset) || (it->second.second != size) )
    {
        fprintf(stderr, "[CRETE ERROR] verify_CpuSate_offset() failed on: "
                "%s, bc[%lu, %lu], qemu[%lu %lu]\n",
                it->first.c_str(), offset, size,
                it->second.first, it->second.second);
        assert(0);
    }
}

void QemuRuntimeInfo::check_trace_tag(uint64_t tt_tag_index, uint64_t tb_index,
        vector<bool>& current_node_br_taken, vector<bool>& current_node_br_taken_semi_explored,
        bool &explored_node) const
{
    assert(tt_tag_index < (m_trace_tag_explored.size() + m_trace_tag_new.size()));

    const crete::CreteTraceTagNode *current_tt_node;

    if(tt_tag_index < m_trace_tag_explored.size()) {
        explored_node = true;
        current_tt_node = &m_trace_tag_explored[tt_tag_index];

        if(tt_tag_index == (m_trace_tag_explored.size() - 1) && !m_trace_tag_semi_explored.empty())
        {
            assert(m_trace_tag_semi_explored.size() == 1);
            current_node_br_taken_semi_explored = m_trace_tag_semi_explored.front().m_br_taken;
        }
    } else {
        explored_node = false;
        current_tt_node = &m_trace_tag_new[tt_tag_index - m_trace_tag_explored.size()];
    }

    // Assumption: conditional br instruction in KLEE and the branches in trace-tag should be matched one by one
    if(current_tt_node->m_tb_count != tb_index)
    {
        fprintf(stderr, "current_tt_node->m_tb_count = %lu, tb_index = %lu\n",
                current_tt_node->m_tb_count, tb_index);
        assert(0 && "[Trace Tag] Assumption broken\n");
    }

    current_node_br_taken = current_tt_node->m_br_taken;
}

// vector::insert(begin, end) would insert [begin, end)
void QemuRuntimeInfo::get_trace_tag_for_tc(uint64_t tt_tag_index,
        crete::creteTraceTag_ty &tt_tag_for_tc,
        vector<bool>& current_node_br_taken_semi_explored) const
{
    assert(tt_tag_index < (m_trace_tag_explored.size() + m_trace_tag_new.size()));

    if(tt_tag_index < m_trace_tag_explored.size())
    {
        tt_tag_for_tc.insert(tt_tag_for_tc.end(), m_trace_tag_explored.begin(),
                m_trace_tag_explored.begin() + tt_tag_index + 1);

        if(tt_tag_index == (m_trace_tag_explored.size() - 1) && !m_trace_tag_semi_explored.empty())
        {
            assert(m_trace_tag_semi_explored.size() == 1);
            current_node_br_taken_semi_explored = m_trace_tag_semi_explored.front().m_br_taken;
        }
    } else
    {
        tt_tag_for_tc.insert(tt_tag_for_tc.end(), m_trace_tag_explored.begin(), m_trace_tag_explored.end());
        tt_tag_for_tc.insert(tt_tag_for_tc.end(), m_trace_tag_new.begin(),
                m_trace_tag_new.begin() + (tt_tag_index - m_trace_tag_explored.size()) + 1);
    }
}

uint64_t QemuRuntimeInfo::get_tt_node_br_num(uint64_t tt_tag_index) const
{
    uint64_t ret;

    assert(tt_tag_index < (m_trace_tag_explored.size() + m_trace_tag_new.size()));

    if(tt_tag_index < m_trace_tag_explored.size())
    {
        ret = m_trace_tag_explored[tt_tag_index].m_br_taken.size();

        if(tt_tag_index == (m_trace_tag_explored.size() - 1) && !m_trace_tag_semi_explored.empty())
        {
             ret += m_trace_tag_semi_explored.front().m_br_taken.size();
        }
    } else {
        ret = m_trace_tag_new[tt_tag_index - m_trace_tag_explored.size()].m_br_taken.size();
    }

    return ret;
}

bool QemuRuntimeInfo::is_tt_node_explored(uint64_t tt_tag_index) const
{
    if(tt_tag_index < m_trace_tag_explored.size())
    {
        return true;
    } else {
        return false;
    }
}


/*****************************/
/* Functions for klee */
QemuRuntimeInfo* qemu_rt_info_initialize()
{
	return new QemuRuntimeInfo;
}

void qemu_rt_info_cleanup(QemuRuntimeInfo *qrt)
{
	delete qrt;
}

void boost::throw_exception(std::exception const & e){
    ;
}

static boost::unordered_set<uint64_t> init_fork_blacklist() {
    boost::unordered_set<uint64_t> list;

    // xxx: hack to disable fork from certain tb
//    list.insert(0xc1199bd0);

    return list;
}

static boost::unordered_set<uint64_t> fork_blacklist = init_fork_blacklist();

bool is_in_fork_blacklist(uint64_t tb_pc)
{
    if (fork_blacklist.find(tb_pc) == fork_blacklist.end())
    {
        return false;
    } else {
        return true;
    }
}
