/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef TRANSFERFAILURE_H
#define TRANSFERFAILURE_H

#include <QString>

enum class TransferFailureReason {
    None,
    ProtocolError,
    PacketOversize,
    InvalidPacketType,
    Timeout,
    TlsError,
    AuthFailed,
    ChecksumFailed,
    InsufficientSpace,
    FileIoError,
    InvalidResumeOffset,
    PeerDisconnected,
    AdmissionBusy,
    ParallelStreamError,
    JournalMismatch,
    Unknown
};

QString transferFailureReasonName(TransferFailureReason reason);
QString transferFailureReasonCode(TransferFailureReason reason);

#endif // TRANSFERFAILURE_H
