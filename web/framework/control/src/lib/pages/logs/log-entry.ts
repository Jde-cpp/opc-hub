import { Guid } from '../../model/guid';
import { ELogLevel } from 'jde-proto/Log';
import { View, ViewField, ViewSerializedArgs, ViewType } from '../../model/ql/view';
import { Query } from '../../services/graphql';
import { TableSchema } from '../../model/ql/schema/table-schema';
import { Field, FieldKind, FieldType } from '../../model/ql/schema/field';
import { StringUtils } from '../../utils/string-utils';

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
	static equals( a:Entry, b:Entry ):boolean{
		return a.time.getTime() === b.time.getTime()
			&& a.userId === b.userId
			&& a.line === b.line
			&& a.level === b.level
			&& a.templateId.equals(b.templateId)
			&& a.fileId.equals(b.fileId)
			&& a.functionId.equals(b.functionId);
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
		return LogEntries.partNames[part] ?? (part.length==2 ? part.toUpperCase() : StringUtils.capitalize( part ));
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
		const vars:Record<string,any> = {limit: this.limit!*2, skip: skip, orderBy: this.sort};//two pages a read:  the first page turn shows what is loaded, the second reads on (LogDetail.onPagerChange)
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
		let view = new LogView( args, schema );
		view.type = ViewType.System;
		return view;
	}
}
/*
export class LogEntry
{
	static parse( tokens:string[], startingIndex:number, instances:Map<number, ApplicationInstance> ):[LogEntry, number,RequestStrings]
	{
		var entry = new LogEntry();
		var i = startingIndex;
		var flags:LogEntry.EFields = parseInt(tokens[i++]);
		var applicationId = parseInt( tokens[i++] );
		entry.applicationInstance = instances.get( applicationId );
		if( !entry.applicationInstance )
			throw new Error( "Do not have application instance"+applicationId.toString() );
		var application = entry.applicationInstance.application;
		var requestStrings = new RequestStrings( applicationId );
		if( flags & LogEntry.EFields.Level )
			entry.level = LogEntry.ELogLevel[tokens[i++]];
		if( flags & LogEntry.EFields.MessageId )
			entry.messageId = parseInt(tokens[i++]);
		if( flags & LogEntry.EFields.FileId )
			entry.fileId = parseInt(tokens[i++]);
		if( flags & LogEntry.EFields.FunctionId )
			entry.functionId = parseInt(tokens[i++]);
		if( flags & LogEntry.EFields.LineNumber )
			entry.lineNumber = parseInt(tokens[i++]);
		if( flags & LogEntry.EFields.UserId )
			entry.userId = parseInt(tokens[i++]);
		if( flags & LogEntry.EFields.ThreadId )
			entry.threadId = parseInt(tokens[i++]);
		if( flags & LogEntry.EFields.Timestamp )
			entry.timestamp = new Date( parseInt(tokens[i++]) );
		if( flags & LogEntry.EFields.Thread )
			entry.applicationInstance.threads.set( entry.threadId, tokens[i++] );
		if( flags & LogEntry.EFields.User )
			application.users.set( entry.userId, tokens[i++] ); //entry.user = tokens[i++];
		else if( entry.userId && !application.users.has(entry.userId) )
			requestStrings.requests.push( [LogEntry.EFields.User, entry.userId] );
		if( flags & LogEntry.EFields.Message )
			application.messages.set( entry.messageId, tokens[i++] ); //entry.user = tokens[i++];
		else if( entry.messageId && !application.messages.has(entry.messageId) )
			requestStrings.requests.push( [LogEntry.EFields.Message, entry.messageId] );
		if( flags & LogEntry.EFields.File )
			application.files.set( entry.fileId, tokens[i++] ); //entry.user = tokens[i++];
		else if( entry.fileId && !application.files.has(entry.fileId) )
			requestStrings.requests.push( [LogEntry.EFields.File, entry.fileId] );
		if( flags & LogEntry.EFields.Function )
			application.functions.set( entry.functionId, tokens[i++] );
		else if( entry.functionId && !application.functions.has(entry.functionId) )
			requestStrings.requests.push( [LogEntry.EFields.Function, entry.functionId] );

		if( flags & LogEntry.EFields.VariableCount )
		{
			entry.variables = new Array<string>();
			let variableCount = parseInt(tokens[i++]);
			for( let variableIndex=0; variableIndex<variableCount; ++variableIndex )
				entry.variables.push( tokens[i++] );
		}

		return [entry,i,requestStrings];
	}
	applicationInstance:ApplicationInstance;
	foo: string="Test Message";
	timestamp: Date;
	messageId: number;
	level:LogEntry.ELogLevel;
	fileId: number;
	//file:string;
	functionId: number;
	lineNumber: number;
	userId: number;
	threadId: number;
	variables:string[];

	get application():Application{ return this.applicationInstance.application; }
	get functionName():string{ return this.functionId ? this.application.functions.get(this.functionId) : null; }
	get user():string{ return this.userId ? this.application.users.get(this.userId) : null; }

	get thread():string{ return this.threadId ? this.applicationInstance.threads.get(this.threadId) : null; }
	get file():string{ return this.fileId ? this.application.files.get(this.fileId) : null; }
	get message():string
	{
		var message = this.messageId ? this.application.messages.get(this.messageId) : null;
		if( message )
		{
			if( this.variables )
			{
				let i; let j=0;
				while( (i=message.indexOf("{}"))!=-1 && j<this.variables.length )
				{
					const foo = message.substring(0, i)+this.variables[j++] + ((i<message.length-2) ? message.substring(0, i+2) : "");
					message = foo;
				}
			}
		}
		return message;
	}
}
export namespace LogEntry
{
	export enum ELogLevel
	{
		Trace = 0,
		Debug = 1,
		Information = 2,
		Warning = 3,
		Error = 4,
		Critical = 5,
		None = 6
	}
	export enum EFields
	{
		None=0,
		Timestamp=0x1,
		MessageId=0x2,
		Message=0x4,
		Level=0x8,
		FileId=0x10,
		File=0x20,
		FunctionId=0x40,
		Function=0x80,
		LineNumber=0x100,
		UserId=0x200,
		User=0x400,
		ThreadId=0x800,
		Thread=0x1000,
		VariableCount=0x2000
	}
}

export class RequestStrings
{
	constructor( applicationInstanceId ){this.applicationInstanceId=applicationInstanceId;}
	applicationInstanceId:number;
	requests:Array<[LogEntry.EFields,number]> = new Array<[LogEntry.EFields,number]>();
	toArray( strings:Array<string> ):void
	{
		strings.push( this.applicationInstanceId.toString() );
		strings.push( this.requests.length.toString() );
		this.requests.forEach(entryId =>
		{
			strings.push( entryId[0].toString() );
			strings.push( entryId[1].toString() );
		});
	}
}
*/
