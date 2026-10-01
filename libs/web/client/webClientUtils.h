#pragma once

namespace Jde::Web::Client{
	// Reorder resolved endpoints so IPv4 precedes IPv6, preserving order within each family.
	// async_connect tries endpoints serially; on Windows "localhost" (and dual-stack hosts)
	// resolves IPv6-first, and each dead IPv6 endpoint stalls the connect ~2s before falling
	// back to IPv4.  Preferring v4 keeps v6 as a fallback but avoids that stall.
	inline std::vector<tcp::endpoint> PreferV4( const tcp::resolver::results_type& results ){
		std::vector<tcp::endpoint> endpoints; endpoints.reserve( results.size() );
		for( const auto& e : results )
			endpoints.push_back( e.endpoint() );
		std::stable_sort( endpoints.begin(), endpoints.end(), [](const tcp::endpoint& a, const tcp::endpoint& b){ return a.address().is_v4() && b.address().is_v6(); } );
		return endpoints;
	}
}