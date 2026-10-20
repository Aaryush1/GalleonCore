// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileTrust.h"
#include <QtTest>
#include <QTemporaryFile>
#ifdef Q_OS_WIN
#include <QScopeGuard>
#include <Windows.h>
#include <aclapi.h>
#include <io.h>
#include <array>
#endif
using namespace galleond;
class TestFileTrust : public QObject
{
    Q_OBJECT
private slots:
    void privateFileCanLaunchApps()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QVERIFY(isTrustedPagesFile(file));
    }
#ifdef Q_OS_WIN
    void windowsAclWritableByEveryoneCannotLaunchApps()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
        PACL original = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        QCOMPARE(GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                 nullptr, nullptr, &original, nullptr, &descriptor), DWORD{ERROR_SUCCESS});
        const auto freeDescriptor = qScopeGuard([descriptor] { LocalFree(descriptor); });
        alignas(DWORD) std::array<BYTE, SECURITY_MAX_SID_SIZE> world{};
        DWORD size = static_cast<DWORD>(world.size());
        QVERIFY(CreateWellKnownSid(WinWorldSid, nullptr, world.data(), &size));
        EXPLICIT_ACCESS_W grant{};
        grant.grfAccessPermissions = FILE_WRITE_DATA;
        grant.grfAccessMode = GRANT_ACCESS;
        grant.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        grant.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        grant.Trustee.ptstrName = reinterpret_cast<LPWSTR>(world.data());
        PACL writable = nullptr;
        QCOMPARE(SetEntriesInAclW(1, &grant, original, &writable), DWORD{ERROR_SUCCESS});
        const auto freeAcl = qScopeGuard([writable] { LocalFree(writable); });
        QCOMPARE(SetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                 nullptr, nullptr, writable, nullptr), DWORD{ERROR_SUCCESS});
        const auto restore = qScopeGuard([handle, original] {
            SetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, original, nullptr);
        });
        QVERIFY(!isTrustedPagesFile(file));
    }
#else
    void writableByOthersCannotLaunchApps()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QVERIFY(file.setPermissions(file.permissions() | QFileDevice::WriteOther));
        QVERIFY(!isTrustedPagesFile(file));
    }
#endif
};
QTEST_GUILESS_MAIN(TestFileTrust)
#include "test_filetrust.moc"
