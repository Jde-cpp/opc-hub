import { NodeKey, INodeId, NodeId, NodeIdJson } from "./node-id";

export type ExNodeIdJson = {nsu?:string,serverIndex?:number,serverindex?:number} & (NodeIdJson | Partial<INodeId>);//NodeId's constructor takes either form - the wire's {ns,i|s|g|b}, or {ns,id} with the identifier already built;  `serverindex` is the gateway's spelling (ExNodeId.cpp)

export class ExNodeId extends NodeId{
	constructor( json: ExNodeIdJson ){
		super(json);
		this.serverIndex = (json.serverIndex ?? json.serverindex)!;
		this.nsu = json.nsu!;
		if( !this.ns && !this.nsu && ExNodeId.defaultNS )
			this.ns = ExNodeId.defaultNS;
	}
	public override equals(obj: ExNodeId) : boolean {
		return super.equals(obj) && (this.serverIndex ?? 0)==(obj.serverIndex ?? 0) && (this.nsu ?? 0)==(obj.nsu ?? 0);
	}

	override get key():NodeKey{
		if( !this._key ){
			let j:any = this.toJson();
			if( this.serverIndex )
				j["serverIndex"] = this.serverIndex;
			this._key = Symbol.for( JSON.stringify(j) );
		}
		return this._key;
  }
	//the standard's spelling, as the gateway prints one (UA_ExpandedNodeId_print):  `svr=1;nsu=urn:x;i=5`, the index and the uri only
	//where set, and the namespace index left out beside a uri
	override uaString():string{
		const base = this.nsu ? new NodeId( {ns: 0, id: this.id} ).uaString() : super.uaString();
		return `${this.serverIndex ? `svr=${this.serverIndex};` : ""}${this.nsu ? `nsu=${this.nsu};` : ""}${base}`;
	}
	//the inverse:  `svr=` and `nsu=` ahead of a NodeId's spelling, each optional
	static override fromUaString( s:string ):ExNodeId{
		const m = /^(?:svr=(\d+);)?(?:nsu=([^;]+);)?(.*)$/.exec( s.trim() );
		if( !m )
			throw new Error( `'${s}' is not an ExpandedNodeId.` );
		const node = NodeId.fromUaString( m[3] );
		return new ExNodeId( {ns: node.ns, id: node.id, ...(m[2] ? {nsu: m[2]} : {}), ...(m[1] ? {serverIndex: +m[1]} : {})} );
	}
	serverIndex:number;
	nsu:string;
//	#key:NodeKey; //for Map
	static defaultNS:number = 0;
}
