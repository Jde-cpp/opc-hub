//A line's floor and its shapes as the gateway's two tables hold them (#481, docs/demo/spec.md *The floor*), and the reads of
//them:  what the floor's resolver needs, in a file of its own so the page's drawing (oee-floor.ts) stays in the page's chunk
//(web/CLAUDE.md 'Routed pages are loaders').

export const oeeFloorKinds = ['Machine', 'Line', 'Zone', 'Wall', 'Aisle', 'Label'] as const;
export type OeeFloorKind = typeof oeeFloorKinds[number];
export type OeePoint = [number, number];//metres from the floor's top-left
export type OeeFloor = { id:number; lineId:number; width:number; height:number; grid:number };
export type OeeFloorShape = { id:number; floorId:number; kind:OeeFloorKind; machineId?:number; lineId?:number; points:OeePoint[]; label?:string; sort:number };
export function toOeeFloor( json:any ):OeeFloor{
	return { id: Number( json.id ), lineId: Number( json.oeeLineId ), width: Number( json.width ), height: Number( json.height ), grid: Number( json.grid ) || 0.5 };
}
//a row's points, `[[x,y],…]` as text or as a list;  one that isn't is no shape's
export function parseFloorPoints( points:unknown ):OeePoint[]|undefined{
	let list = points;
	if( typeof points=='string' ){
		try{ list = JSON.parse( points ); }
		catch{ return undefined; }
	}
	if( !Array.isArray( list ) )
		return undefined;
	const y:OeePoint[] = [];
	for( const p of list ){
		if( !Array.isArray( p ) || p.length!=2 || !p.every( v=>typeof v=='number' && isFinite( v ) ) )
			return undefined;
		y.push( [p[0], p[1]] );
	}
	return y;
}
//a shape as read;  none for a kind outside the six or points that aren't a list of pairs, which the map passes over
export function toOeeFloorShape( json:any ):OeeFloorShape|undefined{
	const kind = <OeeFloorKind>json.kind, points = parseFloorPoints( json.points );
	if( !oeeFloorKinds.includes( kind ) || !points )
		return undefined;
	return {
		id: Number( json.id ), floorId: Number( json.oeeFloorId ), kind, points, sort: Number( json.sort ?? 0 ),
		machineId: json.oeeMachineId==null ? undefined : Number( json.oeeMachineId ), lineId: json.memberLineId==null ? undefined : Number( json.memberLineId ),
		label: json.label || undefined
	};
}