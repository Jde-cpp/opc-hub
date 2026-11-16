import { Service } from '@angular/core';
import { OeeFloor, oeeFloorDefault, OeeFloorRights, OeeFloorShape, toOeeFloor, toOeeFloorRights, toOeeFloorShape } from '../model/oee-floor-rows';
import { OeeLineRef, OeePlant, toOeeLineRef } from '../model/oee-lines';
import type { HistPoster, HistReader } from './history-service';
import { readOeePlant } from './oee-plant';

//The floor page's reads (#481, docs/demo/spec.md *The floor*, *Drawing*):  a line by its slug, the floors, a floor's shapes, the
//plant and a machine's group, each a generic table read gated as tables are, so a caller without Read on them has none;  and
//its editor's rights, new floor and save (#482).  A
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
	//The editor's (#482, spec *Editing*).  What the caller may do to the floors and their shapes, as the gateway's own gates
	//would answer each mutation.
	async rights( reader:HistReader ):Promise<OeeFloorRights>{
		return toOeeFloorRights( ( await reader.query<any>( OeeFloorService.rightsQuery ) )?.oeeFloorRights );
	}
	static readonly rightsQuery = 'oeeFloorRights{ createFloor updateFloor createShape updateShape purgeShape }';
	//a line's floor and its shapes as they are now;  none for a line with no floor
	async floor( reader:HistReader, lineId:number ):Promise<{floor:OeeFloor; shapes:OeeFloorShape[]}|undefined>{
		const floor = ( await this.floors( reader ) ).find( f=>f.lineId==lineId );
		return floor ? { floor, shapes: await this.floorShapes( reader, floor.id ) } : undefined;
	}
	//a line's floor made, of the default size, for the editor to draw on.  A throw is the refusal:  a line that has one, a right withheld.
	async createFloor( poster:HistPoster, lineId:number, size=oeeFloorDefault ):Promise<void>{
		await poster.postQL<any>( OeeFloorService.createFloorMutation, {line: lineId, ...size} );
	}
	static readonly createFloorMutation = 'mutation createOeeFloor( oeeLineId: $line, width: $width, height: $height, grid: $grid ){ id }';
	//The editor's Save, its batch as one request (floorBatchQL).  A throw is the gateway's refusal of one of its mutations, with
	//those before it written.
	async save( poster:HistPoster, request:{ql:string; vars:Record<string,unknown>} ):Promise<void>{
		await poster.postQL<any>( request.ql, request.vars );
	}
}