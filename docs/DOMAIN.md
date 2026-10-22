<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# galleoncore.xyz deployment

The domain is registered at LWS. GitHub Pages continues to serve the static
website built by `.github/workflows/pages.yml`; the included LWS web space is
not a second copy of the site. LWS can host the project's mailboxes separately.

1. Verify ownership of `galleoncore.xyz` for the `Paullux` GitHub account in
   **Settings → Pages → Verified domains**, adding the TXT record requested by
   GitHub to the LWS DNS zone. Keep that TXT record in place.
2. In **Paullux/GalleonCore → Settings → Pages**, set **Custom domain** to
   `galleoncore.xyz`. The Pages site is published with GitHub Actions, so a
   `CNAME` file in `site/` is not required.
3. In LWS DNS, replace only the default web-hosting records for the root (`@`)
   with GitHub Pages' four `A` records: `185.199.108.153`, `185.199.109.153`,
   `185.199.110.153`, `185.199.111.153`. Optionally add GitHub's four `AAAA`
   records from its current documentation. Point `www` via `CNAME` directly to
   `paullux.github.io` (without `/GalleonCore`). Do not use an LWS HTTP forwarding
   rule or a wildcard DNS record.
4. Preserve LWS mail `MX`, SPF, DKIM and DMARC records, plus GitHub's verification
   TXT record. Website `A`/`CNAME` records and mail records serve different jobs.
5. After DNS resolves correctly, enable **Enforce HTTPS** in GitHub Pages and
   verify both `https://galleoncore.xyz/` and
   `https://www.galleoncore.xyz/`, including the downloads page and its JSON
   counters. DNS and certificate provisioning may take up to 24 hours.

The Website workflow tries the custom-domain snapshot first and the legacy
GitHub Pages URL second. This preserves the download-count baseline during DNS
propagation. The other build and release workflows contain no site URL and need
no domain-specific change. Keep the legacy URL as a fallback until the custom
domain and at least two daily snapshots are verified.

The owner confirmed that `contact@galleoncore.xyz` and
`privacy@galleoncore.xyz` both receive mail. The website's terms and privacy
pages use those addresses respectively. The separate security-reporting
address in `SECURITY.md` is unchanged.

References: [GitHub Pages custom domains](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site),
[domain verification](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/verifying-your-custom-domain-for-github-pages).
