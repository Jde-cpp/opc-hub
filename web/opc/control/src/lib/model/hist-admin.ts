import { ISlugRow, Mutation, MutationType, SlugRow, SlugRowProps } from 'jde-framework';
import { ExNodeId } from './ex-node-id';
import { NodeId } from './node-id';
import { HistPathSegment, HistThresholdFields, histThresholdArgs, histThresholdBlank, histThresholdFields, histThresholdFieldsEqual, histThresholdProblem, parseHistPath } from './hist';
import { StatusCode } from './types';

//The gateway historian's tables as the admin pages hold them (#337, spec *Templates*):  hist_templates and hist_groups as rows a
//detail page saves whole, and their members, hist_template_nodes and hist_group_nodes, as rows edited one at a time, since each
//create or purge of one has a live side - a template member resolves in every group of the template, a group node joins the
//group's files and takes an item.

//A row named by its key and name:  a group's connection and template, as the QL joins them (serverConnection{ id name }), and the
//group resolveHistory says holds a node.
export type HistRef = { id:number; name:string };
export function toHistRef( json:any ):HistRef|undefined{ return json && json.id!=null ? { id: Number( json.id ), name: String( json.name ?? '' ) } : undefined; }

//A named node set, as browse paths from a root.  Its members are on its page's Members tab, not in the row.
export class HistTemplate extends SlugRow<HistTemplate>{
	constructor( obj:HistTemplateProps ){ super( HistTemplate.typeName, <SlugRowProps>obj ); }
	get properties():HistTemplate{ return new HistTemplate( this ); }
	static typeName = 'HistTemplate';
}
export type HistTemplateProps = Partial<SlugRowProps>;

//A template applied to one root on one connection, or an ad hoc node list on one, with its own thresholds, the defaults for the
//members that set none.  The connection, the template and the root never change once the row is created - the gateway refuses the
//update (HistGroupMAwait) - so the form fixes them on an existing row:  purge the group and create it again.  `root` is the column's
//text, {"nsu":"urn:…","i":5001}, or what the user typed, a NodeId as ns=2;i=5001 or that json;  blank is the Objects folder.
export class HistGroup extends SlugRow<HistGroup>{
	constructor( obj:HistGroupProps ){
		super( HistGroup.typeName, <SlugRowProps>obj );
		this.guid = obj.guid ?? undefined;
		this.connection = obj.connection ?? toHistRef( obj.serverConnection );
		this.serverConnectionId = HistGroup.toId( obj.serverConnectionId ) ?? this.connection?.id;
		this.template = obj.template ?? toHistRef( obj.histTemplate );
		this.histTemplateId = HistGroup.toId( obj.histTemplateId ) ?? this.template?.id;
		this.root = obj.root ?? undefined;
		this.thresholds = obj.thresholds ? {...obj.thresholds} : histThresholdFields( <Record<string,unknown>><unknown>obj );
	}
	//a key as a select or a row gives it:  a number above 0, else none - 0 is the Template select's "none"
	static toId( v:unknown ):number|undefined{ const n = Number( v ); return v==null || v==='' || !Number.isFinite( n ) || n<=0 ? undefined : n; }
	override get canSave():boolean{ return super.canSave && !!this.serverConnectionId && !this.rootError && !histThresholdProblem( this.thresholds ); }
	override fieldError( field:string ):string|undefined{ return field=='root' ? this.rootError : undefined; }
	//what is wrong with the root typed, or nothing
	get rootError():string|undefined{
		const t = this.root?.trim();
		return t && !parseHistNodeText( t ) ? 'A NodeId:  ns=2;i=5001, or {"nsu":"urn:…","i":5001}.' : undefined;
	}
	override equals( row:ISlugRow ):boolean{
		const o = <HistGroup>row;
		return super.equals( row ) && this.serverConnectionId==o.serverConnectionId && (this.histTemplateId ?? 0)==(o.histTemplateId ?? 0)
			&& (this.root?.trim() ?? '')==(o.root?.trim() ?? '') && histThresholdFieldsEqual( this.thresholds, o.thresholds ?? histThresholdBlank );
	}
	//A create states the source and each threshold set;  an update, the name, slug and description changed and each threshold
	//changed, null for one cleared back to the default (histThresholdArgs).  The source is never sent on an update.
	override mutation( original:HistGroup ):Mutation[]{
		console.assert( this.canSave );
		const args:Record<string,unknown> = super.mutationArgs( original );
		if( args['slug']!==undefined )
			args['slug'] = String( args['slug'] ).trim();
		const create = !original?.id;
		if( create ){
			args['serverConnectionId'] = this.serverConnectionId;
			if( this.histTemplateId )
				args['histTemplateId'] = this.histTemplateId;
			const root = this.root?.trim();
			if( root )
				args['root'] = root;
		}
		Object.assign( args, histThresholdArgs( this.thresholds, create ? undefined : original.thresholds ) );
		return Object.keys( args ).length ? [new Mutation( this.type, this.id, args, create ? MutationType.Create : MutationType.Update )] : [];
	}
	get properties():HistGroup{ return new HistGroup( this ); }
	//the root as a node, or none for the Objects folder or text that isn't one
	get rootNode():ExNodeId|undefined{ const t = this.root?.trim(); return t ? parseHistNodeText( t ) : undefined; }
	guid?:string;
	serverConnectionId?:number;
	histTemplateId?:number;
	root?:string;
	connection?:HistRef;
	template?:HistRef;
	thresholds:HistThresholdFields;
	static typeName = 'HistGroup';
}
//a QL row, with the joins and the threshold columns as it carries them, or a HistGroup, with the fields the form holds
export type HistGroupProps = Partial<SlugRowProps> & { guid?:string|null; serverConnectionId?:number|string|null; histTemplateId?:number|string|null; root?:string|null; serverConnection?:any; histTemplate?:any; connection?:HistRef; template?:HistRef; thresholds?:HistThresholdFields };

//A node as the columns spell it (HistNodeText), {"nsu":"urn:…","i":6042}, or as a user types one, that json or a NodeId's UA
//spelling, ns=2;i=6042, which the gateway spells through the connection's server;  none for text that is neither.
export function parseHistNodeText( text:string ):ExNodeId|undefined{
	const t = text.trim();
	if( !t )
		return undefined;
	if( t.startsWith( '{' ) ){
		try{
			const j = JSON.parse( t );
			return j && typeof j=='object' && ['i', 's', 'g', 'b'].some( k=>j[k]!==undefined ) ? new ExNodeId( j ) : undefined;
		}
		catch{
			return undefined;
		}
	}
	try{
		return new ExNodeId( NodeId.fromUaString( t ).toJson() );
	}
	catch{
		return undefined;
	}
}
//a node's identifier part, without its namespace:  what joins the URI in a column's spelling
function identifier( node:NodeId ):Record<string,unknown>{ const j:Record<string,unknown> = {...node.toJson()}; delete j['ns']; return j; }
//A node as a mutation's `node` or `root` takes it:  by its URI when it has one, else by the server's index, which the gateway turns
//into the URI (HistGroupMAwait SpellAwait).
export function histNodeJson( node:ExNodeId ):Record<string,unknown>{ return node.nsu ? { nsu: node.nsu, ...identifier( node ) } : node.toJson(); }
export function histNodeText( node:ExNodeId ):string{ return node.nsu ? JSON.stringify( histNodeJson( node ) ) : node.uaString(); }
//a node as a page shows it:  by the server's index where `ns` names the URI's, as a node page would, else its URI and identifier
export function histNodeLabel( node:ExNodeId, ns:number|undefined ):string{
	if( ns!=undefined )
		return new NodeId( {...identifier( node ), ns} ).uaString();
	const id = identifier( node );
	const key = Object.keys( id )[0];
	return node.nsu ? `nsu=${node.nsu};${key}=${id[key]}` : node.uaString();
}

//A template's member, as histTemplateNodes answers it:  its path from the group's root and its own thresholds, blank where it
//leaves a group node to inherit the group's.
export type HistTemplateMember = { id:number; templateId:number; path:HistPathSegment[]; thresholds:HistThresholdFields };
export function toHistTemplateMember( json:any ):HistTemplateMember{
	return { id: Number( json.id ), templateId: Number( json.histTemplateId ?? json.histTemplate?.id ?? 0 ), path: parseHistPath( json.path ) ?? [], thresholds: histThresholdFields( json ) };
}
//A group's node, as histGroupNodes answers it:  the row id is the node's index in the group's files, its node by URI, the template
//member it was resolved from, none for one added by hand, and its own thresholds.
export type HistGroupMember = { id:number; node:ExNodeId; templateNodeId?:number; thresholds:HistThresholdFields };
export function toHistGroupMember( json:any ):HistGroupMember{
	const node = parseHistNodeText( String( json.node ?? '' ) ) ?? new ExNodeId( {i: 0} );
	return { id: Number( json.id ), node, templateNodeId: json.histTemplateNodeId==null ? undefined : Number( json.histTemplateNodeId ), thresholds: histThresholdFields( json ) };
}
//A template member as resolveHistory answers it:  its node and Good once it has a row, else why it hasn't - and for Bad_NodeIdExists
//the group that holds the node, another or this one.
export type HistResolvedMember = { templateNode:number; path:HistPathSegment[]; node?:ExNodeId; status:StatusCode; holder?:HistRef };
export function toHistResolved( json:any ):HistResolvedMember[]{
	return ( <any[]>(json?.nodes ?? []) ).map( n=>({ templateNode: Number( n.templateNode ), path: parseHistPath( n.path ) ?? [], node: n.node ? new ExNodeId( n.node ) : undefined, status: <StatusCode>Number( n.status ?? 0 ), holder: toHistRef( n.holder ) }) );
}
//a connection a group can be made on
export type HistConnection = { id:number; slug:string; name:string };
//a group as a template's page lists those made from it
export type HistTemplateGroup = { id:number; name:string; slug:string; connection?:HistRef; root?:string };
//what the node picker answers for each node picked:  the node by its URI, its path from the start as browse names by URI, and its name
export type HistPick = { node:ExNodeId; path:HistPathSegment[]; name:string };