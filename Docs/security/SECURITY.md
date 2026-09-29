# DeckKernel documentation server security boundary

[TOC|Security boundary]

## Current transport decision

The DeckKernel documentation server deliberately uses plain HTTP at the current project
stage.

The safe default configuration is:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<documentation>
   <server address="127.0.0.1" name="localhost" port="8770"/>
</documentation>
```

This means the normal server is reachable only through the local loopback interface.

HTTPS is a planned later extension. It is intentionally not enabled yet because doing so
would require certificate provisioning, trust configuration, renewal, rotation and
private-key handling. DeckKernel does not distribute or require local server certificates
at this stage.

## Non-loopback operation

A user may deliberately configure one concrete non-loopback IP address and a matching
server or DNS name.

That is an explicit deployment decision. For such an installation, firewall rules,
routing, network segmentation and exposure of the selected interface remain the
operator's responsibility.

The server rejects unspecified and multicast bind addresses. Wildcard listeners such as:

```text
0.0.0.0
::
```

are therefore not valid server endpoints.

The intention is to prevent an accidental "listen everywhere" configuration.

## Host validation

Incoming HTTP requests must contain a `Host` header matching either:

- the configured server name;
- the configured bind address;
- or, for a loopback deployment, a normal loopback identity such as `localhost`,
  `127.0.0.1` or `::1`.

The Host check complements the network boundary. It does not replace firewall or routing
controls for a deliberately exposed non-loopback endpoint.

## Read-only behavior

The documentation server is intentionally read-only.

Only HTTP `GET` requests are supported. The server does not provide an upload, edit,
delete or remote command endpoint.

The server renders Markdown from the repository-owned `Docs` tree and serves static
documentation assets from the same controlled root.

## Path safety

Documentation request paths are decoded and validated before filesystem access.

The current implementation rejects:

- parent traversal such as `..`;
- backslashes inside URL path segments;
- drive prefixes;
- invalid documentation paths.

The repository root is determined locally or supplied explicitly by the operator. A
client cannot select arbitrary filesystem roots through an HTTP request.

## Static assets

Browser-side documentation assets are served only below the documentation tree.

The project does not expose source, build, cache, ThirdParty or application directories
as general-purpose static web roots.

Downloaded browser ThirdParty assets will eventually be prepared reproducibly below the
central documentation asset tree. Their distribution model does not change the inbound
security boundary.

## External communication

The local documentation server itself does not need an outbound Internet connection to
serve already prepared documentation.

Other DeckKernel programs, such as the Scryfall examples, may communicate with external
services independently. Those connections use HTTPS and belong to the respective
application's security boundary, not to the inbound documentation-server transport.

## Future HTTPS support

The intended long-term direction is an HTTPS-capable documentation server.

That extension should be added when the project is ready to define a complete certificate
operating model, including:

- certificate source and provisioning;
- trust configuration;
- hostname validation;
- private-key storage;
- expiration handling;
- renewal and rotation.

Until that operational contract exists, the simpler and more transparent model is:
localhost by default, or one deliberately configured concrete interface under operator
responsibility.
