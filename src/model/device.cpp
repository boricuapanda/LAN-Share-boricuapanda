/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "device.h"

#include <QStringList>

Device::Device(const QString &id, const QString &name, const QString &osName, const QHostAddress &addr)
: mId{id}, mName{name}, mOSName{osName}, mAddress{addr}
{
}

bool Device::isValid() const
{
    return (mId != "" && mName != "" && mOSName != "" && mAddress != QHostAddress::Null);
}

QString Device::formatAddress(const QHostAddress& address)
{
    const QString raw = address.toString();
    const QString mappedPrefix = QStringLiteral("::ffff:");
    if (!raw.startsWith(mappedPrefix, Qt::CaseInsensitive))
        return raw;

    const QString mapped = raw.mid(mappedPrefix.size());
    QHostAddress dotted(mapped);
    if (dotted.protocol() == QAbstractSocket::IPv4Protocol)
        return dotted.toString();

    const QStringList groups = mapped.split(QLatin1Char(':'));
    if (groups.size() == 2) {
        bool highOk = false;
        bool lowOk = false;
        const int high = groups.at(0).toInt(&highOk, 16);
        const int low = groups.at(1).toInt(&lowOk, 16);
        if (highOk && lowOk && high >= 0 && high <= 0xffff && low >= 0 && low <= 0xffff) {
            return QStringLiteral("%1.%2.%3.%4")
                .arg((high >> 8) & 0xff)
                .arg(high & 0xff)
                .arg((low >> 8) & 0xff)
                .arg(low & 0xff);
        }
    }

    return raw;
}

QString Device::displayAddress() const
{
    return formatAddress(mAddress);
}

void Device::setId(const QString& id)
{
    mId = id;
}

void Device::setName(const QString& name)
{
    mName = name;
}

void Device::setAddress(const QHostAddress& address)
{
    mAddress = address;
}

void Device::setOSName(const QString& osName)
{
    mOSName = osName;
}

bool Device::operator==(const Device& other) const
{
    return mId == other.getId() && mName == other.getName() && mAddress == other.getAddress();
}

bool Device::operator!=(const Device& other) const
{
    return !((*this) == other);
}
