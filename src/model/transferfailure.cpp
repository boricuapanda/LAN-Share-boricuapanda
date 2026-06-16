/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "transferfailure.h"

QString transferFailureReasonName(TransferFailureReason reason)
{
    switch (reason) {
    case TransferFailureReason::None: return QStringLiteral("none");
    case TransferFailureReason::ProtocolError: return QStringLiteral("protocol_error");
    case TransferFailureReason::PacketOversize: return QStringLiteral("packet_oversize");
    case TransferFailureReason::InvalidPacketType: return QStringLiteral("invalid_packet_type");
    case TransferFailureReason::Timeout: return QStringLiteral("timeout");
    case TransferFailureReason::TlsError: return QStringLiteral("tls_error");
    case TransferFailureReason::AuthFailed: return QStringLiteral("auth_failed");
    case TransferFailureReason::ChecksumFailed: return QStringLiteral("checksum_failed");
    case TransferFailureReason::InsufficientSpace: return QStringLiteral("insufficient_space");
    case TransferFailureReason::FileIoError: return QStringLiteral("file_io_error");
    case TransferFailureReason::InvalidResumeOffset: return QStringLiteral("invalid_resume_offset");
    case TransferFailureReason::PeerDisconnected: return QStringLiteral("peer_disconnected");
    case TransferFailureReason::AdmissionBusy: return QStringLiteral("admission_busy");
    case TransferFailureReason::ParallelStreamError: return QStringLiteral("parallel_stream_error");
    case TransferFailureReason::JournalMismatch: return QStringLiteral("journal_mismatch");
    case TransferFailureReason::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString transferFailureReasonCode(TransferFailureReason reason)
{
    return transferFailureReasonName(reason);
}
