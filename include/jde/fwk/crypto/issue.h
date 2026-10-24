#pragma once
#include "CryptoSettings.h"
#define Φ Γ auto

namespace Jde::Crypto{
	//Issuing and keeping a CryptoSettings' key pair and self-signed certificate on disk.  EnsureKeyCertificate is the one entry
	//every issuer takes - the web listener's, the app client's, the UA server's, the gateway's per-target certificates - so the
	//managed guard, the re-issue predicate and the passcode migration cannot drift between them again (web-certs3 #17, m2-closing #7).
	Φ CreateKey( const CryptoSettings& settings, SRCE )ε->void;//a new RSA pair, the public half written beside the private.
	//validity is a test seam:  production always issues for a year, and the expiry branch of ReissueReason is otherwise untestable (nothing else mints an expired certificate).
	Φ IssueCertificate( const CryptoSettings& settings, std::chrono::seconds validity = std::chrono::days{365}, SRCE )ε->void;
	//why the certificate on disk can no longer stand for `settings` - missing, issued on another key, expired or expiring within a
	//day, or a SAN drifted from the configured one - empty if it can.  An unreadable certificate throws (#3(b)).
	Φ ReissueReason( const CryptoSettings& settings, SRCE )ε->string;
	//a managed private key written in the clear, before privateKey.passcode was set, rewritten encrypted with it - the same key, so
	//its certificates and every trust built on them stand.  A no-op without a passcode, or for a key that is missing or already
	//encrypted.  The Linux install starts the services itself, so "set the passcode before the first start" could not be followed,
	//and one set after changed nothing (reviews/m4-closing.md #11).
	Φ EncryptPrivateKey( const CryptoSettings& settings, SRCE )ε->void;
	Φ CreateKeyCertificate( const CryptoSettings& settings, SRCE )ε->void;
	//the pair and certificate as `settings` describes them:  made when absent, the key encrypted once a passcode appears, the
	//certificate re-issued on the same key for any ReissueReason;  an unmanaged pair used as found.  Serialized process-wide:
	//IssueCertificate writes the PEM in place, and two callers for one path - a test connection and a connect, two tests - read
	//each other's half-written file (#250 review #13).
	Φ EnsureKeyCertificate( const CryptoSettings& settings, SRCE )ε->void;
	//`found` is the level a certificate left in place is logged at:  Information - a listener's at start says which one it presents - or
	//Debug for a check that reruns, the gateway's per connect.  Making or re-issuing one is Information either way.
	Φ EnsureKeyCertificate( const CryptoSettings& settings, ELogLevel found, SRCE )ε->void;
}
#undef Φ