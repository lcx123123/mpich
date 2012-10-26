/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2012 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "ptl_impl.h"

#undef FUNCNAME
#define FUNCNAME dequeue_req
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static void dequeue_req(const ptl_event_t *e)
{
    int found;
    MPID_Request *const rreq = e->user_ptr;
    int s_len, r_len;

    found = MPIDI_CH3U_Recvq_DP(rreq);
    MPIU_Assert(found);

    rreq->status.MPI_ERROR = MPI_SUCCESS;
    rreq->status.MPI_SOURCE = NPTL_MATCH_GET_RANK(e->match_bits);
    rreq->status.MPI_TAG = NPTL_MATCH_GET_TAG(e->match_bits);

    MPID_Datatype_get_size_macro(rreq->dev.datatype, r_len);
    r_len *= rreq->dev.user_count;

    s_len = NPTL_HEADER_GET_LENGTH(e->hdr_data);

    if (s_len > r_len) {
        /* truncated data */
        rreq->status.count = r_len;
        MPIU_ERR_SET2(rreq->status.MPI_ERROR, MPI_ERR_TRUNCATE, "**truncate", "**truncate %d %d", s_len, r_len);
    }
    
    rreq->status.count = s_len;
}

#undef FUNCNAME
#define FUNCNAME handler_recv_complete
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int handler_recv_complete(const ptl_event_t *e)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *const rreq = e->user_ptr;
    int ret;
    int i;
    MPIDI_STATE_DECL(MPID_STATE_HANDLER_RECV_COMPLETE);

    MPIDI_FUNC_ENTER(MPID_STATE_HANDLER_RECV_COMPLETE);
    
    MPIU_Assert(e->type == PTL_EVENT_REPLY || e->type == PTL_EVENT_PUT || e->type == PTL_EVENT_PUT_OVERFLOW);
    
    if (REQ_PTL(rreq)->md != PTL_INVALID_HANDLE) {
        ret = PtlMDRelease(REQ_PTL(rreq)->md);
        MPIU_ERR_CHKANDJUMP1(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmdrelease", "**ptlmdrelease %s", MPID_nem_ptl_strerror(ret));
    }

    for (i = 0; i < MPID_NEM_PTL_NUM_CHUNK_BUFFERS; ++i)
        if (REQ_PTL(rreq)->chunk_buffer[i])
            MPIU_Free(REQ_PTL(rreq)->chunk_buffer[i]);
    
    MPIDI_CH3U_Request_complete(rreq);

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_HANDLER_RECV_COMPLETE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME handler_recv_dequeue_complete
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int handler_recv_dequeue_complete(const ptl_event_t *e)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *const rreq = e->user_ptr;
    MPIDI_STATE_DECL(MPID_STATE_HANDLER_RECV_DEQUEUE_COMPLETE);

    MPIDI_FUNC_ENTER(MPID_STATE_HANDLER_RECV_DEQUEUE_COMPLETE);
    
    MPIU_Assert(e->type == PTL_EVENT_PUT || e->type == PTL_EVENT_PUT_OVERFLOW);
    
    dequeue_req(e);

    if (e->type == PTL_EVENT_PUT_OVERFLOW) {
        /* unpack the data from unexpected buffer */
        int is_contig;
        MPI_Aint last;

        MPID_Datatype_is_contig(rreq->dev.datatype, &is_contig);
        if (is_contig) {
            MPIU_Memcpy(rreq->dev.user_buf, e->start, e->mlength);
        } else {
            last = e->mlength;
            MPID_Segment_pack(rreq->dev.segment_ptr, 0, &last, e->start);
            MPIU_ERR_CHKANDJUMP(last != e->mlength, mpi_errno, MPI_ERR_OTHER, "**dtypemismatch");
        }
    }
    
    mpi_errno = handler_recv_complete(e);

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_HANDLER_RECV_DEQUEUE_COMPLETE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME handler_recv_unpack_complete
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int handler_recv_unpack_complete(const ptl_event_t *e)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *const rreq = e->user_ptr;
    void *buf;
    MPIDI_STATE_DECL(MPID_STATE_HANDLER_RECV_UNPACK_COMPLETE);

    MPIDI_FUNC_ENTER(MPID_STATE_HANDLER_RECV_UNPACK_COMPLETE);
    
    MPIU_Assert(e->type == PTL_EVENT_REPLY || e->type == PTL_EVENT_PUT || e->type == PTL_EVENT_PUT_OVERFLOW);

    if (e->type == PTL_EVENT_PUT_OVERFLOW)
        buf = e->start;
    else
        buf = REQ_PTL(rreq)->chunk_buffer[0];

    mpi_errno = MPID_nem_ptl_unpack_byte(rreq->dev.segment_ptr, rreq->dev.segment_first, e->mlength,
                                         buf, &REQ_PTL(rreq)->overflow[0]);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    mpi_errno = handler_recv_complete(e);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_HANDLER_RECV_UNPACK_COMPLETE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME handler_recv_dequeue_unpack_complete
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int handler_recv_dequeue_unpack_complete(const ptl_event_t *e)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_HANDLER_RECV_DEQUEUE_UNPACK_COMPLETE);

    MPIDI_FUNC_ENTER(MPID_STATE_HANDLER_RECV_DEQUEUE_UNPACK_COMPLETE);
    
    MPIU_Assert(e->type == PTL_EVENT_PUT || e->type == PTL_EVENT_PUT_OVERFLOW);

    dequeue_req(e);
    mpi_errno = handler_recv_unpack_complete(e);

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_HANDLER_RECV_DEQUEUE_UNPACK_COMPLETE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME handler_recv_dequeue_large
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int handler_recv_dequeue_large(const ptl_event_t *e)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *const rreq = e->user_ptr;
    MPIDI_VC_t *vc;
    MPID_nem_ptl_vc_area *vc_ptl;
    int ret;
    int dt_contig;
    MPIDI_msg_sz_t data_sz;
    MPID_Datatype *dt_ptr;
    MPI_Aint dt_true_lb;
    MPI_Aint last;
    MPIU_CHKPMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_HANDLER_RECV_DEQUEUE_LARGE);

    MPIDI_FUNC_ENTER(MPID_STATE_HANDLER_RECV_DEQUEUE_LARGE);
    
    MPIU_Assert(e->type == PTL_EVENT_PUT || e->type == PTL_EVENT_PUT_OVERFLOW);

    MPIDI_Comm_get_vc(rreq->comm, NPTL_MATCH_GET_RANK(e->match_bits), &vc);
    vc_ptl = VC_PTL(vc);
    
    dequeue_req(e);

    if (!(e->hdr_data & NPTL_LARGE)) {
        /* all data has already been received; we're done */
        mpi_errno = handler_recv_complete(e);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        goto fn_exit;
    }

    MPIU_Assert (e->mlength == PTL_LARGE_THRESHOLD);

    /* we need to GET the rest of the data from the sender's buffer */
    MPIDI_Datatype_get_info(rreq->dev.user_count, rreq->dev.datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    if (dt_contig) {
        /* recv buffer is contig */
        if (e->type == PTL_EVENT_PUT_OVERFLOW)
            /* copy data from unexpected buffer */
            MPIU_Memcpy(e->start, rreq->dev.user_buf, PTL_LARGE_THRESHOLD);
        
        REQ_PTL(rreq)->event_handler = handler_recv_complete;
        ret = PtlGet(MPIDI_nem_ptl_global_md, (ptl_size_t)rreq->dev.user_buf + PTL_LARGE_THRESHOLD, data_sz - PTL_LARGE_THRESHOLD,
                     vc_ptl->id, vc_ptl->ptg, e->match_bits, 0, rreq);
        MPIU_ERR_CHKANDJUMP1(ret, mpi_errno, MPI_ERR_OTHER, "**ptlget", "**ptlget %s", MPID_nem_ptl_strerror(ret));
        goto fn_exit;
    }

    /* noncontig recv buffer */

    if (e->type == PTL_EVENT_PUT_OVERFLOW) {
        /* unpack data from unexpected buffer first */
        rreq->dev.segment_first = 0;
        last = PTL_LARGE_THRESHOLD;
        MPID_Segment_unpack(rreq->dev.segment_ptr, rreq->dev.segment_first, &last, e->start);
        MPIU_Assert(last == PTL_LARGE_THRESHOLD);
        rreq->dev.segment_first = PTL_LARGE_THRESHOLD;
    }

    
    last = rreq->dev.segment_size;
    rreq->dev.iov_count = MPID_IOV_LIMIT;
    MPID_Segment_pack_vector(rreq->dev.segment_ptr, rreq->dev.segment_first, &last, rreq->dev.iov, &rreq->dev.iov_count);

    if (last == rreq->dev.segment_size) {
        /* Rest of message fits in one IOV */
        ptl_md_t md;

        md.start = rreq->dev.iov;
        md.length = rreq->dev.iov_count;
        md.options = PTL_IOVEC;
        md.eq_handle = MPIDI_nem_ptl_eq;
        md.ct_handle = PTL_CT_NONE;
        ret = PtlMDBind(MPIDI_nem_ptl_ni, &md, &REQ_PTL(rreq)->md);
        MPIU_ERR_CHKANDJUMP1(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmdbind", "**ptlmdbind %s", MPID_nem_ptl_strerror(ret));

        REQ_PTL(rreq)->event_handler = handler_recv_complete;
        ret = PtlGet(REQ_PTL(rreq)->md, 0, rreq->dev.segment_size - rreq->dev.segment_first, vc_ptl->id, vc_ptl->ptg,
                     e->match_bits, 0, rreq);
        MPIU_ERR_CHKANDJUMP1(ret, mpi_errno, MPI_ERR_OTHER, "**ptlget", "**ptlget %s", MPID_nem_ptl_strerror(ret));
        goto fn_exit;
    }
        
    /* message won't fit in a single IOV, allocate buffer and unpack when received */
    /* FIXME: For now, allocate a single large buffer to hold entire message */
    MPIU_CHKPMEM_MALLOC(REQ_PTL(rreq)->chunk_buffer[0], void *, rreq->dev.segment_size - rreq->dev.segment_first, mpi_errno, "chunk_buffer");

    REQ_PTL(rreq)->event_handler = handler_recv_unpack_complete;
    ret = PtlGet(MPIDI_nem_ptl_global_md, (ptl_size_t)REQ_PTL(rreq)->chunk_buffer[0],
                 rreq->dev.segment_size - rreq->dev.segment_first, vc_ptl->id, vc_ptl->ptg, e->match_bits, 0, rreq);
    MPIU_ERR_CHKANDJUMP1(ret, mpi_errno, MPI_ERR_OTHER, "**ptlget", "**ptlget %s", MPID_nem_ptl_strerror(ret));

 fn_exit:
    MPIU_CHKPMEM_COMMIT();
 fn_exit2:
    MPIDI_FUNC_EXIT(MPID_STATE_HANDLER_RECV_DEQUEUE_LARGE);
    return mpi_errno;
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit2;
}


#undef FUNCNAME
#define FUNCNAME handler_recv_dequeue_unpack_large
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int handler_recv_dequeue_unpack_large(const ptl_event_t *e)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *const rreq = e->user_ptr;
    MPIDI_VC_t *vc;
    MPID_nem_ptl_vc_area *vc_ptl;
    int ret;
    void *buf;
    MPIU_CHKPMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_HANDLER_RECV_DEQUEUE_UNPACK_LARGE);

    MPIDI_FUNC_ENTER(MPID_STATE_HANDLER_RECV_DEQUEUE_UNPACK_LARGE);
    MPIU_Assert(e->type == PTL_EVENT_PUT || e->type == PTL_EVENT_PUT_OVERFLOW);

    MPIDI_Comm_get_vc(rreq->comm, NPTL_MATCH_GET_RANK(e->match_bits), &vc);
    vc_ptl = VC_PTL(vc);

    dequeue_req(e);

    if (!(e->hdr_data & NPTL_LARGE)) {
        /* all data has already been received; we're done */
        mpi_errno = handler_recv_unpack_complete(e);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        goto fn_exit;
    }

    if (e->type == PTL_EVENT_PUT_OVERFLOW)
        buf = e->start;
    else
        buf = REQ_PTL(rreq)->chunk_buffer[0];

    MPIU_Assert(e->mlength == PTL_LARGE_THRESHOLD);
    mpi_errno = MPID_nem_ptl_unpack_byte(rreq->dev.segment_ptr, rreq->dev.segment_first, PTL_LARGE_THRESHOLD,
                                         buf, &REQ_PTL(rreq)->overflow[0]);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    rreq->dev.segment_first += PTL_LARGE_THRESHOLD;
    MPIU_Free(REQ_PTL(rreq)->chunk_buffer[0]);

    MPIU_CHKPMEM_MALLOC(REQ_PTL(rreq)->chunk_buffer[0], void *, rreq->dev.segment_size - rreq->dev.segment_first, mpi_errno, "chunk_buffer");
    
    REQ_PTL(rreq)->event_handler = handler_recv_unpack_complete;
    ret = PtlGet(MPIDI_nem_ptl_global_md, (ptl_size_t)REQ_PTL(rreq)->chunk_buffer[0],
                 rreq->dev.segment_size - rreq->dev.segment_first, vc_ptl->id, vc_ptl->ptg, e->match_bits, 0, rreq);
    MPIU_ERR_CHKANDJUMP1(ret, mpi_errno, MPI_ERR_OTHER, "**ptlget", "**ptlget %s", MPID_nem_ptl_strerror(ret));

 fn_exit:
    MPIU_CHKPMEM_COMMIT();
 fn_exit2:
    MPIDI_FUNC_EXIT(MPID_STATE_HANDLER_RECV_DEQUEUE_UNPACK_LARGE);
    return mpi_errno;
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit2;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_recv_posted
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_recv_posted(MPIDI_VC_t *vc, MPID_Request *rreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ptl_vc_area *const vc_ptl = VC_PTL(vc);
    ptl_me_t me;
    int dt_contig;
    MPIDI_msg_sz_t data_sz;
    MPID_Datatype *dt_ptr;
    MPI_Aint dt_true_lb;
    MPI_Aint last;
    ptl_process_t id_any;
    int ret;
    MPIU_CHKPMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_RECV_POSTED);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_RECV_POSTED);

    id_any.phys.nid = PTL_NID_ANY;
    id_any.phys.pid = PTL_PID_ANY;

    MPID_nem_ptl_init_req(rreq);
    
    me.ct_handle = PTL_CT_NONE;
    me.uid = PTL_UID_ANY;
    me.options = ( PTL_ME_OP_PUT | PTL_ME_IS_ACCESSIBLE | PTL_ME_EVENT_LINK_DISABLE |
                   PTL_ME_EVENT_UNLINK_DISABLE | PTL_ME_USE_ONCE );
    if (vc == NULL) {
        /* MPI_ANY_SOURCE receive */
        me.match_id = id_any;
    } else {
        if (!vc_ptl->id_initialized) {
            mpi_errno = MPID_nem_ptl_init_id(vc);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        me.match_id = vc_ptl->id;
    }
    
    me.match_bits = NPTL_MATCH(rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id, rreq->dev.match.parts.rank);
    me.ignore_bits = NPTL_MATCH_IGNORE;
    me.min_free = 0;

    MPIDI_Datatype_get_info(rreq->dev.user_count, rreq->dev.datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    if (data_sz < PTL_LARGE_THRESHOLD) {
        if (dt_contig) {
            /* small contig message */
            me.start = rreq->dev.user_buf;
            me.length = data_sz;
            REQ_PTL(rreq)->event_handler = handler_recv_dequeue_complete;
        } else {
            /* small noncontig */
            rreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1(rreq->dev.segment_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPID_Segment_init(rreq->dev.user_buf, rreq->dev.user_count, rreq->dev.datatype, rreq->dev.segment_ptr, 0);
            rreq->dev.segment_first = 0;
            rreq->dev.segment_size = data_sz;

            last = rreq->dev.segment_size;
            rreq->dev.iov_count = MPID_IOV_LIMIT;
            MPID_Segment_pack_vector(rreq->dev.segment_ptr, 0, &last, rreq->dev.iov, &rreq->dev.iov_count);

            if (last == rreq->dev.segment_size) {
                /* entire message fits in IOV */
                me.start = rreq->dev.iov;
                me.length = rreq->dev.iov_count;
                me.options |= PTL_IOVEC;
                REQ_PTL(rreq)->event_handler = handler_recv_dequeue_complete;
            } else {
                /* IOV is not long enough to describe entire message: recv into
                   buffer and unpack later */
                MPIU_CHKPMEM_MALLOC(REQ_PTL(rreq)->chunk_buffer[0], void *, data_sz, mpi_errno, "chunk_buffer");
                me.start = REQ_PTL(rreq)->chunk_buffer[0];
                me.length = data_sz;
                REQ_PTL(rreq)->event_handler = handler_recv_dequeue_unpack_complete;
            }
        }
    } else {
        /* Large message: Create an ME for the first chunk of data, then do a GET for the rest */
        if (dt_contig) {
            /* large contig message */
            me.start = rreq->dev.user_buf;
            me.length = PTL_LARGE_THRESHOLD;
            REQ_PTL(rreq)->event_handler = handler_recv_dequeue_large;
        } else {
            /* large noncontig */
            rreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1(rreq->dev.segment_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPID_Segment_init(rreq->dev.user_buf, rreq->dev.user_count, rreq->dev.datatype, rreq->dev.segment_ptr, 0);
            rreq->dev.segment_first = 0;
            rreq->dev.segment_size = data_sz;

            last = PTL_LARGE_THRESHOLD;
            rreq->dev.iov_count = MPID_IOV_LIMIT;
            MPID_Segment_pack_vector(rreq->dev.segment_ptr, 0, &last, rreq->dev.iov, &rreq->dev.iov_count);

            if (last == PTL_LARGE_THRESHOLD) {
                /* first chunk fits in IOV */
                rreq->dev.segment_first = last;
                me.start = rreq->dev.iov;
                me.length = rreq->dev.iov_count;
                me.options |= PTL_IOVEC;
                REQ_PTL(rreq)->event_handler = handler_recv_dequeue_large;
            } else {
                /* IOV is not long enough to describe the first chunk: recv into
                   buffer and unpack later */
                MPIU_CHKPMEM_MALLOC(REQ_PTL(rreq)->chunk_buffer[0], void *, PTL_LARGE_THRESHOLD, mpi_errno, "chunk_buffer");
                me.start = REQ_PTL(rreq)->chunk_buffer[0];
                me.length = PTL_LARGE_THRESHOLD;
                REQ_PTL(rreq)->event_handler = handler_recv_dequeue_unpack_large;
            }
        }
        
    }

    ret = PtlMEAppend(MPIDI_nem_ptl_ni, MPIDI_nem_ptl_pt, &me, PTL_PRIORITY_LIST, rreq, &REQ_PTL(rreq)->me);
    MPIU_ERR_CHKANDJUMP1(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmeappend", "**ptlmeappend %s", MPID_nem_ptl_strerror(ret));
    DBG_MSG_MEAPPEND("REG", vc->pg_rank, me, rreq);

 fn_exit:
    MPIU_CHKPMEM_COMMIT();
 fn_exit2:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_RECV_POSTED);
    return mpi_errno;
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit2;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_anysource_posted
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
void MPID_nem_ptl_anysource_posted(MPID_Request *rreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_ANYSOURCE_POSTED);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_ANYSOURCE_POSTED);

    mpi_errno = MPID_nem_ptl_recv_posted(NULL, rreq);

    /* FIXME: This function is void, so we can't return an error.  This function
       cannot return an error because the queue functions (where the posted_recv
       hooks are called) return no error code. */
    MPIU_Assertp(mpi_errno == MPI_SUCCESS);

    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_ANYSOURCE_POSTED);
}

#undef FUNCNAME
#define FUNCNAME cancel_recv
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int cancel_recv(MPID_Request *rreq, int *cancelled)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_CANCEL_RECV);

    MPIDI_FUNC_ENTER(MPID_STATE_CANCEL_RECV);

    MPIU_Assert(0 && "FIXME: Need to implement cancel_recv");

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_CANCEL_RECV);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_anysource_matched
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_anysource_matched(MPID_Request *rreq)
{
    int mpi_errno = MPI_SUCCESS;
    int cancelled = 0;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_ANYSOURCE_MATCHED);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_ANYSOURCE_MATCHED);

    mpi_errno = cancel_recv(rreq, &cancelled);
    /* FIXME: This function is does not return an error because the queue
       functions (where the posted_recv hooks are called) return no error
       code. */
    MPIU_Assertp(mpi_errno == MPI_SUCCESS);

    return !cancelled;

    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_ANYSOURCE_MATCHED);
}



#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_cancel_recv
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_cancel_recv(MPIDI_VC_t *vc,  MPID_Request *rreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_CANCEL_RECV);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_CANCEL_RECV);

    MPIU_Assert(0 && "implement me");

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_CANCEL_RECV);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}
