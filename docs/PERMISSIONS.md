# Permission tracker

Every tool whose licence does not already permit redistribution, and the status of
the request to its author. **A tool moves into `tools/` only when its status here is
`granted`.** Until then it stays user-supplied.

## Status

| tool | author | licence today | status |
|---|---|---|---|
| **SREP** | Bulat Ziganshin | none (all rights reserved) | **not requested** |
| **CCM 1.20a** | Christian Martelock | none stated | not requested |
| **crush / miniCRUSH** | Ilya Muravyov | none stated | not requested |
| **ashford** | (see source headers) | none stated | not requested |
| **DeflOpt** | Ben Jos Walbeehm | none stated | not requested |
| **lzari** | Haruhiko Okumura (from *C: The Complete Reference*) | none stated | not requested |
| **nanozip** | Sami Runsas | closed source | not requested |
| **WinRK** | Malcolm Taylor | commercial | not requested |
| **Document Press** | — | commercial | not requested |

## Why asking is worth doing

It is the cheapest legal route by a wide margin, and the answer is often yes — these
are hobbyist and research tools whose authors generally want them used, and a
project that credits them properly is a good advertisement. A single email can turn
a permanently blocked tool into a shipped one.

It also has a useful side effect: an author who says yes sometimes says *"here is a
newer version"* or *"here is what I would do differently"*, which is worth more than
the binary.

## Template

Short, specific, easy to answer. Do not ask for a licence change; ask for
permission to redistribute.

```
Subject: Permission to redistribute <TOOL> alongside an open-source archiver

Hello <AUTHOR>,

I maintain heavenzip, an MIT-licensed archiver orchestrator that chains existing
compression tools as separate processes:
https://github.com/drsamonline/heavenzip

<TOOL> is one of the tools it can drive. At the moment I cannot include it in a
release, because it carries no licence file and therefore defaults to all rights
reserved.

Would you be willing to grant permission to redistribute the unmodified binary
alongside heavenzip, with full credit to you and a link to your project? Any of
these would work for me:

  1. a permissive licence (MIT / BSD / Apache-2.0) for the binary,
  2. a statement that redistribution of the unmodified binary is permitted,
  3. permission limited to heavenzip releases, if you would rather not generalise.

I am not asking you to change anything about the project or to support it — only
for the right to ship the binary you already published, unchanged.

If you would rather not, that is completely fine: heavenzip will list <TOOL> as
user-supplied and point people to your site.

Thank you for <TOOL> either way — <ONE SPECIFIC THING IT DOES WELL>.

Best regards,
Dr Sohil Momin
```

## If an author cannot be reached

Then the tool stays user-supplied, which is not a failure. heavenzip's job is to
*route*, and a router that supports a tool it does not ship is working correctly.
The only thing that must never happen is shipping it and calling the credit line a
licence.
