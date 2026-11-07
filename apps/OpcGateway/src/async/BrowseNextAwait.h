#pragma once
#include "../uatypes/Browse.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//BrowseNext from the continuation points a browse answered with:  the references the server held back at its cap, results[i] for
	//points[i], each with a point of its own when the server holds back more (TypeRegistry's walk of the Structure subtree).  `release`
	//lets them go instead, answering no references - the walk's when it stops part way, as ReleaseBrowseAwait is the others'.  The points
	//are copied, so the browse's answer may go first;  with none it resumes at once.  Throws for a service refusal.
	struct BrowseNextAwait final : UAServiceAwait<Browse::Response, UA_BrowseNextResponse, UA_TYPES_BROWSENEXTREQUEST, UA_TYPES_BROWSENEXTRESPONSE>{
		BrowseNextAwait( const UA_BrowseResponse& browse, sp<UAClient> client, bool release=false, SRCE )ι;
		α Suspend()ι->void override;
		α Points()Ι->uint{ return _points.size(); }
	private:
		α Request()ι->void* override;
		α Convert( UA_BrowseNextResponse&& response )ε->Browse::Response override;
		vector<Owned<UA_ByteString,UA_TYPES_BYTESTRING>> _points;//the request's array:  owned here until the answer.
		UA_BrowseNextRequest _request{};//borrows _points, so never cleared.
		bool _release;
	};
}