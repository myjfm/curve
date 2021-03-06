/*
 *  Copyright (c) 2020 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: curve
 * Created Date: 18-9-25
 * Author: wudemiao
 */

#include "src/client/request_sender.h"
#include <glog/logging.h>

#include <algorithm>

#include "proto/chunk.pb.h"
#include "src/common/timeutility.h"
#include "src/client/request_closure.h"
#include "src/common/location_operator.h"

using curve::common::TimeUtility;

namespace curve {
namespace client {

static void EmptyDeleter(void* ptr) {}

int RequestSender::Init(const IOSenderOption_t& ioSenderOpt) {
    if (0 != channel_.Init(serverEndPoint_, NULL)) {
        LOG(ERROR) << "failed to init channel to server, id: " << chunkServerId_
                   << ", "<< serverEndPoint_.ip << ":" << serverEndPoint_.port;
        return -1;
    }
    iosenderopt_ = ioSenderOpt;
    ClientClosure::SetFailureRequestOption(iosenderopt_.failRequestOpt);

    return 0;
}

int RequestSender::ReadChunk(ChunkIDInfo idinfo,
                             uint64_t sn,
                             off_t offset,
                             size_t length,
                             uint64_t appliedindex,
                             const RequestSourceInfo& sourceInfo,
                             ClientClosure *done) {
    brpc::ClosureGuard doneGuard(done);

    RequestClosure* rc = static_cast<RequestClosure*>(done->GetClosure());
    MetricHelper::IncremRPCRPSCount(rc->GetMetric(), OpType::READ);
    rc->SetStartTime(TimeUtility::GetTimeofDayUs());

    brpc::Controller *cntl = new brpc::Controller();
    cntl->set_timeout_ms(
    std::max(rc->GetNextTimeoutMS(),
        iosenderopt_.failRequestOpt.chunkserverRPCTimeoutMS));
    done->SetCntl(cntl);
    ChunkResponse *response = new ChunkResponse();
    done->SetResponse(response);
    done->SetChunkServerID(chunkServerId_);
    done->SetChunkServerEndPoint(serverEndPoint_);

    ChunkRequest request;
    request.set_optype(curve::chunkserver::CHUNK_OP_TYPE::CHUNK_OP_READ);
    request.set_logicpoolid(idinfo.lpid_);
    request.set_copysetid(idinfo.cpid_);
    request.set_chunkid(idinfo.cid_);
    request.set_offset(offset);
    request.set_size(length);

    if (!sourceInfo.cloneFileSource.empty()) {
        request.set_clonefilesource(sourceInfo.cloneFileSource);
        request.set_clonefileoffset(sourceInfo.cloneFileOffset);
    }

    if (iosenderopt_.chunkserverEnableAppliedIndexRead && appliedindex > 0) {
        request.set_appliedindex(appliedindex);
    }
    ChunkService_Stub stub(&channel_);
    stub.ReadChunk(cntl, &request, response, doneGuard.release());

    return 0;
}

int RequestSender::WriteChunk(ChunkIDInfo idinfo,
                              uint64_t sn,
                              const char *buf,
                              off_t offset,
                              size_t length,
                              const RequestSourceInfo& sourceInfo,
                              ClientClosure *done) {
    brpc::ClosureGuard doneGuard(done);

    RequestClosure* rc = static_cast<RequestClosure*>(done->GetClosure());
    MetricHelper::IncremRPCRPSCount(rc->GetMetric(), OpType::WRITE);
    rc->SetStartTime(TimeUtility::GetTimeofDayUs());

    DVLOG(9) << "Sending request, buf header: "
             << " buf: " << *(unsigned int *)buf;
    brpc::Controller *cntl = new brpc::Controller();
    cntl->set_timeout_ms(
    std::max(rc->GetNextTimeoutMS(),
        iosenderopt_.failRequestOpt.chunkserverRPCTimeoutMS));
    done->SetCntl(cntl);
    ChunkResponse *response = new ChunkResponse();
    done->SetResponse(response);
    done->SetChunkServerID(chunkServerId_);
    done->SetChunkServerEndPoint(serverEndPoint_);

    ChunkRequest request;
    request.set_optype(curve::chunkserver::CHUNK_OP_TYPE::CHUNK_OP_WRITE);
    request.set_logicpoolid(idinfo.lpid_);
    request.set_copysetid(idinfo.cpid_);
    request.set_chunkid(idinfo.cid_);
    request.set_sn(sn);
    request.set_offset(offset);
    request.set_size(length);

    if (!sourceInfo.cloneFileSource.empty()) {
        request.set_clonefilesource(sourceInfo.cloneFileSource);
        request.set_clonefileoffset(sourceInfo.cloneFileOffset);
    }

    cntl->request_attachment().append_user_data(
        const_cast<char*>(buf), length, EmptyDeleter);
    ChunkService_Stub stub(&channel_);
    stub.WriteChunk(cntl, &request, response, doneGuard.release());

    return 0;
}

int RequestSender::ReadChunkSnapshot(ChunkIDInfo idinfo,
                                     uint64_t sn,
                                     off_t offset,
                                     size_t length,
                                     ClientClosure *done) {
    brpc::ClosureGuard doneGuard(done);

    RequestClosure* rc = static_cast<RequestClosure*>(done->GetClosure());
    brpc::Controller *cntl = new brpc::Controller();
    cntl->set_timeout_ms(
    std::max(rc->GetNextTimeoutMS(),
        iosenderopt_.failRequestOpt.chunkserverRPCTimeoutMS));
    done->SetCntl(cntl);
    ChunkResponse *response = new ChunkResponse();
    done->SetResponse(response);

    ChunkRequest request;
    request.set_optype(curve::chunkserver::CHUNK_OP_TYPE::CHUNK_OP_READ_SNAP);
    request.set_logicpoolid(idinfo.lpid_);
    request.set_copysetid(idinfo.cpid_);
    request.set_chunkid(idinfo.cid_);
    request.set_sn(sn);
    request.set_offset(offset);
    request.set_size(length);
    ChunkService_Stub stub(&channel_);
    stub.ReadChunkSnapshot(cntl, &request, response, doneGuard.release());

    return 0;
}

int RequestSender::DeleteChunkSnapshotOrCorrectSn(ChunkIDInfo idinfo,
                                       uint64_t correctedSn,
                                       ClientClosure *done) {
    brpc::ClosureGuard doneGuard(done);

    RequestClosure* rc = static_cast<RequestClosure*>(done->GetClosure());
    brpc::Controller *cntl = new brpc::Controller();
    cntl->set_timeout_ms(
    std::max(rc->GetNextTimeoutMS(),
        iosenderopt_.failRequestOpt.chunkserverRPCTimeoutMS));
    done->SetCntl(cntl);
    ChunkResponse *response = new ChunkResponse();
    done->SetResponse(response);

    ChunkRequest request;
    request.set_optype(curve::chunkserver::CHUNK_OP_TYPE::CHUNK_OP_DELETE_SNAP);
    request.set_logicpoolid(idinfo.lpid_);
    request.set_copysetid(idinfo.cpid_);
    request.set_chunkid(idinfo.cid_);
    request.set_correctedsn(correctedSn);
    ChunkService_Stub stub(&channel_);
    stub.DeleteChunkSnapshotOrCorrectSn(cntl,
                                        &request,
                                        response,
                                        doneGuard.release());
    return 0;
}

int RequestSender::GetChunkInfo(ChunkIDInfo idinfo,
                                ClientClosure *done) {
    brpc::ClosureGuard doneGuard(done);

    RequestClosure* rc = static_cast<RequestClosure*>(done->GetClosure());
    brpc::Controller *cntl = new brpc::Controller();
    cntl->set_timeout_ms(
    std::max(rc->GetNextTimeoutMS(),
        iosenderopt_.failRequestOpt.chunkserverRPCTimeoutMS));
    done->SetCntl(cntl);
    GetChunkInfoResponse *response = new GetChunkInfoResponse();
    done->SetResponse(response);

    GetChunkInfoRequest request;
    request.set_logicpoolid(idinfo.lpid_);
    request.set_copysetid(idinfo.cpid_);
    request.set_chunkid(idinfo.cid_);
    ChunkService_Stub stub(&channel_);
    stub.GetChunkInfo(cntl, &request, response, doneGuard.release());
    return 0;
}

int RequestSender::CreateCloneChunk(ChunkIDInfo idinfo,
                                ClientClosure *done,
                                const std::string &location,
                                uint64_t correntSn,
                                uint64_t sn,
                                uint64_t chunkSize) {
    brpc::ClosureGuard doneGuard(done);

    RequestClosure* rc = static_cast<RequestClosure*>(done->GetClosure());
    brpc::Controller *cntl = new brpc::Controller();
    cntl->set_timeout_ms(
    std::max(rc->GetNextTimeoutMS(),
        iosenderopt_.failRequestOpt.chunkserverRPCTimeoutMS));
    done->SetCntl(cntl);
    ChunkResponse *response = new ChunkResponse();
    done->SetResponse(response);

    ChunkRequest request;
    request.set_optype(
        curve::chunkserver::CHUNK_OP_TYPE::CHUNK_OP_CREATE_CLONE);
    request.set_logicpoolid(idinfo.lpid_);
    request.set_copysetid(idinfo.cpid_);
    request.set_chunkid(idinfo.cid_);
    request.set_location(location);
    request.set_sn(sn);
    request.set_correctedsn(correntSn);
    request.set_size(chunkSize);

    ChunkService_Stub stub(&channel_);
    stub.CreateCloneChunk(cntl, &request, response, doneGuard.release());
}

int RequestSender::RecoverChunk(const ChunkIDInfo& idinfo,
                                ClientClosure *done,
                                uint64_t offset,
                                uint64_t len) {
    brpc::ClosureGuard doneGuard(done);

    RequestClosure* rc = static_cast<RequestClosure*>(done->GetClosure());
    brpc::Controller *cntl = new brpc::Controller();
    cntl->set_timeout_ms(
    std::max(rc->GetNextTimeoutMS(),
        iosenderopt_.failRequestOpt.chunkserverRPCTimeoutMS));
    done->SetCntl(cntl);
    ChunkResponse *response = new ChunkResponse();
    done->SetResponse(response);

    ChunkRequest request;
    request.set_optype(curve::chunkserver::CHUNK_OP_TYPE::CHUNK_OP_RECOVER);
    request.set_logicpoolid(idinfo.lpid_);
    request.set_copysetid(idinfo.cpid_);
    request.set_chunkid(idinfo.cid_);
    request.set_offset(offset);
    request.set_size(len);

    ChunkService_Stub stub(&channel_);
    stub.RecoverChunk(cntl, &request, response, doneGuard.release());
}

int RequestSender::ResetSender(ChunkServerID chunkServerId,
                               butil::EndPoint serverEndPoint) {
    chunkServerId_ = chunkServerId;
    serverEndPoint_ = serverEndPoint;
    return Init(iosenderopt_);
}

}   // namespace client
}   // namespace curve
