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
	init_concolics();

	CRETE_CK(read_debug_cpuState_offsets(););
}

QemuRuntimeInfo::~QemuRuntimeInfo()
{
    cleanup_concolics();
}

static void check_cpu_state(klee::ExecutionState &state, klee::ObjectState *os_current_cpu_state,
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

void QemuRuntimeInfo::sync_cpuState(klee::ExecutionState &state, klee::ObjectState *wos, uint64_t tb_index) {
    if(tb_index >= m_streamed_tb_count) {
        read_streamed_trace();
        assert((m_streamed_tb_count - tb_index) == m_cpuStateSyncTables.size());
    }

    uint64_t adjusted_tb_index = tb_index - (m_streamed_tb_count - m_cpuStateSyncTables.size());

    cpuStateSyncTable_ty cpuStateSyncTable = m_cpuStateSyncTables[adjusted_tb_index];

    CRETE_DBG(
    cerr << "-------------------------------------------------------\n";
    cerr << "tb-" << dec << tb_index << ": sync_cpuState()\n";
    );

    if(!cpuStateSyncTable.first) return;

    assert(!cpuStateSyncTable.second.empty());

    check_cpu_state(state, wos, cpuStateSyncTable.second, tb_index);

    CRETE_DBG(cerr << " concretized elements: \n";);
    for(vector<CPUStateElement>::const_iterator it = cpuStateSyncTable.second.begin();
            it != cpuStateSyncTable.second.end(); ++it) {
        if(it->m_name.find("debug") != string::npos) {
            continue;
        }

        assert(it->m_data.size() == it->m_size);
        wos->write_n(it->m_offset,it->m_data);
        CRETE_DBG(fprintf(stderr, "(%s:%lu): [", it->m_name.c_str(), it->m_size);
        for(uint64_t i = 0; i < it->m_size; ++i) {
            cerr << hex << " 0x" << (uint32_t)it->m_data[i];
        }
        cerr << "]\n";
        );
    }
}

const memoSyncTable_ty& QemuRuntimeInfo::get_memoSyncTable(uint64_t tb_index)
{
    if(tb_index >= m_streamed_tb_count)
    {
        read_streamed_trace();
        assert((m_streamed_tb_count - tb_index) == m_memoSyncTables.size());
    }

    uint64_t adjusted_tb_index = tb_index - (m_streamed_tb_count - m_memoSyncTables.size());
    assert(adjusted_tb_index < m_memoSyncTables.size());

    return m_memoSyncTables[adjusted_tb_index];
}

void QemuRuntimeInfo::printMemoSyncTable(uint64_t tb_index)
{
	memoSyncTable_ty temp_mst = m_memoSyncTables[tb_index];

	cerr << "memoSyncTable content of index " << dec << tb_index << ": ";

	if(temp_mst.empty()){
		cerr << " NULL\n";
	} else {
		cerr << "size = " << temp_mst.size() << ":\n";
		for(memoSyncTable_ty::iterator m_it = temp_mst.begin();
				m_it != temp_mst.end(); ++m_it){

			cerr << hex << "0x" << m_it->first << ": (0x " << m_it->second << "); ";
		}

		cerr << dec << endl;
	}
}

concolics_ty QemuRuntimeInfo::get_concolics() const
{
	return m_concolics;
}

void QemuRuntimeInfo::check_file_symbolics()
{
    ifstream ifs("dump_mo_symbolics.txt", ios_base::binary);
    vector<string> symbolics;
    string symbolic_entry;
    while(getline(ifs, symbolic_entry, '\n')) {
    	symbolics.push_back(symbolic_entry);
    }

    ifs.close();

    set<string> unique_symbolics;
    vector<string> output_symbolics;
    for(vector<string>::iterator i = symbolics.begin();
    		i != symbolics.end(); ++i) {
    	if(unique_symbolics.insert(*i).second){
    		output_symbolics.push_back(*i);
    	}
    }

    ofstream ofs("dump_mo_symbolics.txt", ios_base::binary);
    for(vector<string>::iterator i = output_symbolics.begin();
        		i != output_symbolics.end(); ++i) {
    	ofs << *i << '\n';
    }
    ofs.close();
}

//Get the information of concolic variables from file "dump_mo_symbolics" and "concrete_inputs.bin"
void QemuRuntimeInfo::init_concolics()
{
    using namespace crete;

    check_file_symbolics();

    ifstream inputs("concrete_inputs.bin", ios_base::in | ios_base::binary);
    assert(inputs && "failed to open concrete_inputs file!");
    const TestCase tc = read_serialized(inputs);

    m_base_tc_issue_index = tc.get_issue_index();

    // Get the concrete value of conoclic variables and put them in a map indexed by name
    vector<TestCaseElement> tc_elements = tc.get_elements();
    map<string, cv_concrete_ty> map_concrete_value;
    for(vector<TestCaseElement>::const_iterator it = tc_elements.begin();
    		it != tc_elements.end(); ++it) {
    	string c_name(it->name.begin(), it->name.end());
    	cv_concrete_ty pair_concrete_value(it->data_size,
    			it->data);
    	map_concrete_value.insert(pair<string, cv_concrete_ty>(c_name,
    			pair_concrete_value));
    }
    assert(map_concrete_value.size() == tc_elements.size());

    ifstream ifs("dump_mo_symbolics.txt");
    assert(ifs && "failed to open dump_mo_symbolics file!");

    string name;
    vector<uint8_t> concrete_value;
    uint64_t data_size;
    uint64_t guest_addr;
    uint64_t host_addr;

    uint64_t name_addr;
    uint64_t fake_val;

    map<string, cv_concrete_ty>::iterator map_it;
    ConcolicVariable *cv;
    string line;

    while(getline(ifs, line)) {
        stringstream sym_ss(line);
        sym_ss >> name;
        sym_ss >> name_addr;
        sym_ss >> fake_val;
        sym_ss >> data_size;
        sym_ss >> guest_addr;
        sym_ss >> host_addr;

        map_it = map_concrete_value.find(name);
        assert(map_it != map_concrete_value.end() &&
        		"concrete value for a concolic variable is not found!\n");

        concrete_value = map_it->second.second;
        data_size= map_it->second.first;

        cv = new ConcolicVariable(name, concrete_value, data_size,
        		guest_addr, host_addr);
        m_concolics.push_back(cv);
    }

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

void QemuRuntimeInfo::cleanup_concolics()
{
	while(!m_concolics.empty()){
		ConcolicVariable *ptr_cv = m_concolics.back();
		m_concolics.pop_back();
		delete ptr_cv;
	}
}

void QemuRuntimeInfo::print_memoSyncTables()
{
	uint64_t temp_tb_count = 0;
	cerr << "[Memo Sync Table]\n";
	for(memoSyncTables_ty::const_iterator it = m_memoSyncTables.begin();
			it != m_memoSyncTables.end(); ++it){
		if(it->empty()){
			cerr << "tb_count: " << dec << temp_tb_count++ << ": NULL\n";
		} else {
			cerr << "tb_count: " << dec << temp_tb_count++
					<< ", size = " << it->size() << ": ";
			for(memoSyncTable_ty::const_iterator m_it = it->begin();
					m_it != it->end(); ++m_it){
				cerr << hex << "0x" << m_it->first << ": (0x " << (uint64_t)m_it->second  << "); ";
			}

			cerr << dec << endl;
		}
	}
}

void QemuRuntimeInfo::print_cpuSyncTable(uint64_t tb_index) const
{
    if(!m_cpuStateSyncTables[tb_index].first) {
        cerr << "tb-" << dec << tb_index << ": cpuSyncTable is empty\n";
    }

    cerr << "tb-" << dec << tb_index << ": cpuSyncTable size = "
            << m_cpuStateSyncTables[tb_index].second.size() << endl;

    for(vector<CPUStateElement>::const_iterator it = m_cpuStateSyncTables[tb_index].second.begin();
            it != m_cpuStateSyncTables[tb_index].second.end(); ++it) {
        cerr << it->m_name << ": " << it->m_size << " bytes"
                << " [";
        for(uint64_t i = 0; i < it->m_size; ++i) {
            cerr << " 0x"<< hex << (uint32_t)it->m_data[i];
        }
        cerr << "]\n";
    }
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
    uint32_t read_amt_cst = read_cpuSyncTables();
    uint32_t read_amt_dbg_cst = read_debug_cpuSyncTables();
    uint32_t read_amt_mst = read_memoSyncTables();

    assert(read_amt_cst == read_amt_dbg_cst);
    assert(read_amt_cst == read_amt_mst);

    m_streamed_tb_count += read_amt_cst;
    ++m_streamed_index;
}

uint32_t QemuRuntimeInfo::read_cpuSyncTables()
{
    stringstream ss;
    ss << "dump_sync_cpu_states." << m_streamed_index << ".bin";
    ifstream i_sm(ss.str().c_str(), ios_base::binary);
    if(!i_sm.good()) {
        cerr << "[Crete Error] can't find file " << ss.str() << endl;
        assert(0);
    }

    boost::archive::binary_iarchive ia(i_sm);
    m_cpuStateSyncTables.clear();
    ia >> m_cpuStateSyncTables;

    return m_cpuStateSyncTables.size();
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

uint32_t QemuRuntimeInfo::read_memoSyncTables()
{
    stringstream ss;
    ss << "dump_new_sync_memos." << m_streamed_index << ".bin";
    ifstream i_sm(ss.str().c_str(), ios_base::binary);
    if(!i_sm.good()) {
        cerr << "[Crete Error] can't find file " << ss.str() << endl;
        assert(0);
    }

    boost::archive::binary_iarchive ia(i_sm);
    m_memoSyncTables.clear();
    ia >> m_memoSyncTables;

    return m_memoSyncTables.size();
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
        klee::ObjectState *os, uint64_t tb_index_input) {
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
