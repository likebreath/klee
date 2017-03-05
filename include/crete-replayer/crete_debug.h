#ifndef CRETE_DEBUG_H
#define CRETE_DEBUG_H

#define CRETE_CROSS_CHECK

//#define CRETE_DEBUG_GENERAL
//#define CRETE_DEBUG_MEMORY

//#define CRETE_DEBUG_XMM
//#define CRETE_DEBUG_FLOAT

//#define CRETE_DEBUG_TAINT_ANALYSIS

//#define CRETE_DEBUG_TRACE_TAG

#define PRINT_TB_INDEX 0xfffffff

#ifdef CRETE_DEBUG_GENERAL
#define CRETE_DBG(x) do { x } while(0)
#else
#define CRETE_DBG(x) do { } while(0)
#endif

#ifdef CRETE_DEBUG_MEMORY
#define CRETE_DBG_MEMORY(x) do { x } while(0)
#else
#define CRETE_DBG_MEMORY(x) do { } while(0)
#endif

#ifdef CRETE_CROSS_CHECK
#define CRETE_CK(x) do { x } while(0)
#else
#define CRETE_CK(x) do { } while(0)
#endif

#ifdef CRETE_DEBUG_XMM
#define CRETE_DBG_XMM(x) do { x } while(0)
#else
#define CRETE_DBG_XMM(x) do { } while(0)
#endif

#ifdef CRETE_DEBUG_FLOAT
#define CRETE_DBG_FLT(x) do { x } while(0)
#else
#define CRETE_DBG_FLT(x) do { } while(0)
#endif

#ifdef CRETE_DEBUG_TAINT_ANALYSIS
#define CRETE_DBG_TA(x) do { x } while(0)
#else
#define CRETE_DBG_TA(x) do { } while(0)
#endif

#ifdef CRETE_DEBUG_TRACE_TAG
#define CRETE_DBG_TT(x) do { x } while(0)
#else
#define CRETE_DBG_TT(x) do { } while(0)
#endif

#include <crete/trace_tag.h>
#include <fstream>

namespace crete
{
namespace debug
{

inline void print_trace_tag(const crete::creteTraceTag_ty& trace_tag)
{
    for(crete::creteTraceTag_ty::const_iterator it = trace_tag.begin();
            it != trace_tag.end(); ++it) {
        fprintf(stderr, "tb-%lu: pc=%p, last_opc = %p",
                it->m_tb_count, (void *)it->m_tb_pc,
                (void *)(uint64_t)it->m_last_opc);
        fprintf(stderr, ", br_taken = ");
        crete::print_br_taken(it->m_br_taken);
        fprintf(stderr,"\n");
    }
}

} // namespace debug
} // namespace crete

#endif // CRETE_DEBUG_H
