// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileTrust.h"
#include <Windows.h>
#include <AclAPI.h>
#include <io.h>
#include <memory>
#include <vector>
#include <cstddef>

namespace galleond {
namespace {
struct FreeSecurity
{
    void operator()(void *value) const { LocalFree(value); }
};
bool trustedSid(PSID sid, PSID user)
{
    return IsValidSid(sid) && (EqualSid(sid, user)
        || IsWellKnownSid(sid, WinLocalSystemSid)
        || IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
}
}

bool isTrustedPagesFile(const QFile &file)
{
    if (!file.isOpen() || file.handle() < 0)
        return false;
    // Inspect the open file, not a second pathname lookup that could race a replacement.
    const auto native = _get_osfhandle(file.handle());
    if (native == -1)
        return false;
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD result = GetSecurityInfo(reinterpret_cast<HANDLE>(native), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &acl, nullptr, &descriptor);
    const std::unique_ptr<void, FreeSecurity> security(descriptor);
    if (result != ERROR_SUCCESS || !owner || !acl || !IsValidAcl(acl))
        return false; // A null DACL grants everyone full access.

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0 || size > 65536) {
        CloseHandle(token);
        return false;
    }
    std::vector<std::byte> storage(size);
    const bool gotUser = GetTokenInformation(token, TokenUser, storage.data(), size, &size) != 0;
    CloseHandle(token);
    if (!gotUser)
        return false;
    const auto *user = reinterpret_cast<const TOKEN_USER *>(storage.data());
    if (!trustedSid(owner, user->User.Sid))
        return false;

    constexpr ACCESS_MASK Writes = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA
        | FILE_WRITE_ATTRIBUTES | WRITE_DAC | WRITE_OWNER | DELETE | GENERIC_WRITE | GENERIC_ALL;
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *raw = nullptr;
        if (!GetAce(acl, index, &raw) || !raw)
            return false;
        const auto *header = static_cast<const ACE_HEADER *>(raw);
        if ((header->AceFlags & INHERIT_ONLY_ACE) != 0)
            continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE)
            continue; // Ignoring denies is conservative: they cannot grant a write.
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceSize < sizeof(ACCESS_ALLOWED_ACE))
            return false; // Unknown/conditional ACEs must not weaken the policy.
        const auto *entry = static_cast<const ACCESS_ALLOWED_ACE *>(raw);
        if ((entry->Mask & Writes) != 0
            && !trustedSid(const_cast<DWORD *>(&entry->SidStart), user->User.Sid))
            return false;
    }
    return true;
}
} // namespace galleond
