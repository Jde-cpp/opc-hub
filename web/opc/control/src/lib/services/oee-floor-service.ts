import { Service } from '@angular/core';
import { OeeFloor, OeeFloorShape, toOeeFloor, toOeeFloorShape } from '../model/oee-floor-rows';
import { OeeLineRef, OeePlant, toOeeLineRef } from '../model/oee-lines';
import type { HistReader } from './history-service';
import { readOeePlant } from './oee-plant';

//The floor page's reads (#481, docs/demo/spec.md *The floor*, *Drawing*):  a line by its slug, the floors, a floor's shapes, the
//plant and a machine's group, each a generic table read gated as tables are, so a caller without Read on them has none.  A
//service of its own, beside OeeService, since the floor's resolver sits in the initial bundle and the page's service would bring
//the OEE pages' model with it.
@Service()
export class OeeFloorService{
	//a live line by its page's slug, with its calendar;  none where there isn't one
	async line( reader:HistReader, slug:string ):Promise<(OeeLineRef & {calendarId?:number})|undefined>{
		const row = ( <any[]>(( await reader.query<any>( OeeFloorService.lineQuery, {slug} ) )?.oeeLines ?? []) )[0];
		return row ? { ...toOeeLineRef( row ), ...(row.calendarId==null ? {} : {calendarId: Number( row.calendarId )}) } : undefined;
	}
	static readonly lineQuery = 'oeeLines( slug: $slug, deleted: null ){ id name slug level calendarId }';
	//the floors, a line's at most one
	async floors( reader:HistReader ):Promise<OeeFloor[]>{
		return ( <any[]>(( await reader.query<any>( OeeFloorService.floorsQuery ) )?.oeeFloors ?? []) ).map( toOeeFloor );
	}
	static readonly floorsQuery = 'oeeFloors{ id oeeLineId width height grid }';
	//a floor's shapes in their rows' order;  one of an unknown kind or with points that aren't a list of pairs is passed over
	async floorShapes( reader:HistReader, floorId:number ):Promise<OeeFloorShape[]>{
		return ( <any[]>(( await reader.query<any>( OeeFloorService.floorShapesQuery, {floor: floorId} ) )?.oeeFloorShapes ?? []) ).flatMap( s=>{ const shape = toOeeFloorShape( s ); return shape ? [shape] : []; } );
	}
	static readonly floorShapesQuery = 'oeeFloorShapes( oeeFloorId: $floor ){ id oeeFloorId kind oeeMachineId memberLineId points label sort }';
	//the gateway's machines, lines and memberships, as the dashboard reads them
	plant( reader:HistReader ):Promise<OeePlant>{ return readOeePlant( reader ); }
	//a machine's historian group, for the panel to link its page and its root node:  its slug, its connection's and its root
	async group( reader:HistReader, id:number ):Promise<{slug:string; connection?:string; root?:string}|undefined>{
		const row = ( <any[]>(( await reader.query<any>( OeeFloorService.groupQuery, {id} ) )?.histGroups ?? []) )[0];
		return row ? { slug: String( row.slug ?? '' ), connection: row.serverConnection?.slug ? String( row.serverConnection.slug ) : undefined, root: row.root || undefined } : undefined;
	}
	static readonly groupQuery = 'histGroups( id: $id ){ id slug root serverConnection{ id slug } }';
}