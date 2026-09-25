# Connecting to an AzerothCore realm

Use **External Realm** with WotLK 3.3.5a build 12340 and a normal server account.
The authentication server and world server are separate TCP endpoints.

| Setting | Value to use |
|---|---|
| Login address | Reachable LAN IP or DNS hostname of the authentication server; no `http://`, URL path or port suffix |
| Login port | The configured authserver port; normally `3724`, not the worldserver port |
| World address | The address advertised by the selected realm in the server's `realmlist` table |
| World port | The configured worldserver port; normally `8085` |
| Client build | WotLK 3.3.5a / `12340` |

On a LAN, the server must advertise an address reachable from the PS4.
`127.0.0.1` and `localhost` refer to the console itself when received by the
client. Correct the server's advertised realm address settings instead of
forcing every world connection to use the authentication host: those services
may intentionally be hosted on different machines. For Docker deployments,
follow the server guide and leave `localAddress` at its default. Ensure bind settings
and the firewall allow both TCP ports on the intended network.

## Locating the failure

- **Cannot connect:** check the address, auth port, server process and network.
  Update 2.11 uses the PlayStation `SO_NBIO` socket option for nonblocking TCP.
  The 2.10 `Failed to make TCP socket nonblocking ... errno=13` message means
  local socket setup failed before the server handshake. If 2.11 reports
  `TCP setsockopt(SO_NBIO) failed`, preserve the complete error line and record
  the console firmware and homebrew loader version.
- **Account/password rejected:** check the account on the server. Repeated
  automatic retries are intentionally not used for account rejection.
- **Realm list appears but world connection fails:** inspect the advertised
  realm address/port, worldserver process, bind address and firewall.
- **Server identity verification fails:** the proof did not match. The client
  will not bypass verification; confirm the server/client protocol and retain
  both client and server logs for diagnosis.
- **Disconnect after entering the world:** preserve the logs and record the
  character, zone and action. Authentication success does not establish that
  every gameplay opcode or server module is supported.

Client logs are under `/data/wow_ps/wowps/logs/`. Keep `boot`, `wowps` and
`vulkan_icd` logs together. Redact account names and private addresses before
posting them publicly; never publish passwords or saved login hashes.

## Update 2.11 console check

1. Back up `/data/wow_ps/saves/local_realm/` and configuration files. Install
   the release and check that the displayed version is `2.11`. The `02.11` package
   version is lower than higher-numbered development builds; do not delete
   saves or configuration to resolve an installer conflict.
2. Connect using the server's reachable address and auth port. Verify login,
   realm selection, character selection and world entry.
3. Move, cast, open an NPC dialogue, log out, reconnect and change character.
4. Check Single Player and, with matching 2.11 peers/content, Host/Join LAN.

The host tests use a synthetic local SRP/authentication peer and a socket-option
adapter to exercise denied descriptor-control calls. They do not execute the
PS4 kernel or claim a live AzerothCore server acceptance run.

Server reference: [AzerothCore networking](https://www.azerothcore.org/wiki/networking).
