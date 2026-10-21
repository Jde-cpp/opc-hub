import { Guid } from '../../model/guid';
import { ELogLevel } from 'jde-proto/Log';
import { View, ViewField, ViewSerializedArgs, ViewType } from '../../model/ql/view';
import { Query } from '../../services/graphql';
import { TableSchema } from '../../model/ql/schema/table-schema';
import { Field, FieldKind, FieldType } from '../../model/ql/schema/field';
import { capitalize } from 'jde-spa';

const TagSeparator = ".";//logTags.h TagSeparator — how the server spells a combined tag as one name

export type Entry={
	templateId:Guid;
	argIds:Guid[];
	level:ELogLevel;
	tags:string[];
	line:number;
	time:Date;
	userId:number;
	fileId:Guid;
	functionId:Guid;

	selected?:boolean;
	hidden:boolean;
}
export type LogEntriesRest={ entries:Entry[]; strings:{id:string, value:string}[]; }
export class LogEntries{
	constructor( queryResult:LogEntriesRest ){
		this.entries = queryResult.entries;
		this.entries.forEach( e=>{
			e.level = ELogLevel[<string><any>e.level as keyof typeof ELogLevel];
			e.templateId = new Guid( <string><any>e.templateId );
			e.fileId = new Guid( <string><any>e.fileId );
			e.functionId = new Guid( <string><any>e.functionId );
			e.time = new Date( e.time );
			e.argIds.forEach( (id, index)=>{
				e.argIds[index] = new Guid( <string><any>id );
			});
		});
		queryResult.strings.forEach( s=>this.strings.set(s.id, s.value) );
	}
	//the arguments too:  without them, rows one line logs in a loop within one millisecond were all one row, and paging
	//dropped all but the first as repeats.
	static equals( a:Entry, b:Entry ):boolean{
		return a.time.getTime() === b.time.getTime()
			&& a.userId === b.userId
			&& a.line === b.line
			&& a.level === b.level
			&& a.templateId.equals(b.templateId)
			&& a.fileId.equals(b.fileId)
			&& a.functionId.equals(b.functionId)
			&& a.argIds.length === b.argIds.length
			&& a.argIds.every( (id, i)=>id.equals(b.argIds[i]) );
	}
	//a combined tag reaches us in ELogTags bit order, which is alphabetical - "read.server.socket".  logTags.h spells
	//that same tag "socket.client.read": transport, then role, then direction, which is the order that reads.  ranked
	//tags lead in this order, the rest keep the order they arrived in.
	static tagOrder = ["socket", "http", "client", "server", "read", "write"];
	static orderTags( tags:readonly string[] ):string[]{
		const rank = ( tag:string )=>{
			const i = LogEntries.tagOrder.indexOf( tag );
			return i<0 ? LogEntries.tagOrder.length : i;
		};
		return [...tags].sort( (a,b)=>rank(a)-rank(b) );//sort is stable, so the unranked tail is left as the wire had it
	}
	//display only — the wire value stays as the server spells it, because ToLogTags parses that spelling back and it
	//is the key the settings are stored under.
	static tagName( tag:string ):string{
		return LogEntries.orderTags( tag.split(TagSeparator) ).map( LogEntries.partName ).join( TagSeparator );
	}
	//a part whose display spelling capitalize() cannot guess - the wire keeps the long name, the row shows the short one.
	static partNames: Record<string,string> = { socket: "Sock" };
	private static partName( part:string ):string{
		return LogEntries.partNames[part] ?? (part.length==2 ? part.toUpperCase() : capitalize( part ));
	}
	static columns: Record<string,string> = {
		time: "Time",
		level: "Level",
		line: "Line",
		message: "Message",
		file: "File",
		function: "Function",
		tags: "Tags",
		user: "User"
	};
	entries:Entry[];
	strings:Map<string,string> = new Map<string,string>();
}
export class LogView extends View{
	override query( skip:number, level?:ELogLevel ):Query{
		const vars:Record<string,any> = {limit: this.limit!*3, skip: skip, orderBy: this.sort};//three pages a read;  a turn onto a page not all loaded reads on (LogDataSource.turnTo)
		let args = "limit: $limit, skip: $skip, orderBy: $orderBy";
		//Trace is every level, so send no filter at all; above it the minimum is expressed as gt the level below.
		if( level!=undefined && level>ELogLevel.Trace ){
			args += ", level:{gt:$level}";
			vars["level"] = level-1;
		}
		const q = `logs( ${args} ){ entries{templateId argIds level tags line time userId fileId functionId} strings{id value} }`;
		return { text: q, vars: vars };
	}
	static schema:TableSchema = new TableSchema( {
	name: "LogEntry",
	fields: [
		new Field( {name: "time", type: new FieldType({ kind: FieldKind.SCALAR, name: "Date"})} ),
		new Field( {name: "level", type: new FieldType({ kind: FieldKind.OBJECT, name: "LogLevel" })} ),
		new Field( {name: "message", type: new FieldType({kind: FieldKind.SCALAR, name: "String"})} ),
		new Field( {name: "tags", type: new FieldType({kind: FieldKind.LIST, name:"LogTags"})} ),
		new Field( {name: "user", ofType: { kind: FieldKind.SCALAR, name: "String" } }),
		new Field( {name: "file", ofType: { kind: FieldKind.SCALAR, name: "String" } }),
		new Field( {name: "line", ofType: { kind: FieldKind.SCALAR, name: "Number" } }),
		new Field( {name: "function", ofType: { kind: FieldKind.SCALAR, name: "String" } }),
	] } );
	static default():LogView{
		let schema = LogView.schema;
		let toViewField = (name:string) => new ViewField( {qlField: schema.find(name)} );
		let args:ViewSerializedArgs = {
			name: undefined,
			collectionName: "logs",
			limit: 21,
			showSelector: false,
			sort: [ { active: 'time', direction: 'desc' } ],
			fields: [
				toViewField("time"),
				toViewField("level"),
				toViewField("message")
			]
		};
		let view = LogView.fromJson( args, schema );
		view.type = ViewType.System;
		return view;
	}
}