import { toBrowse } from '../../model/types';

//SEGMENT_NAME for node pages:  below /gateways/<gateway>/<connection>/ each segment is a node's browse name - `5~pump1`
//(namespace 5, pump1), or the bare name in the connection's default namespace - and the breadcrumbs and Recently visited
//show the name as the server spells it, rather than the raw segment or the title-cased fallback.
export function nodeSegmentName( segments:string[], index:number ):string|undefined{
	if( segments[0]!='gateways' || index<3 || index>=segments.length )
		return undefined;
	let segment = segments[index];
	try{
		segment = decodeURIComponent( segment );
	}catch{}//a malformed escape:  name it as it stands
	return String( toBrowse(segment, undefined).name );
}
