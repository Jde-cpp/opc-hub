export type OpcId = string;
export type Ns = number;
export type StatusCode = number;
export const scBadUnexpectedError:StatusCode = 0x80010000;

export interface ILocalizedText{
	locale: string|undefined;
	text: string;
}
export enum EAccess{
	None = 0,
	Read = 1,
	Write = 2,
	HistoryRead = 4,
	HistoryWrite = 8,
	SemanticChange = 0x10,
	StatusWrite = 0x20,
	TimestampWrite = 0x40,
	All = 0x7F
}
export const toLocalizedText = ( value:any ):ILocalizedText=>{
	return typeof(value)=="string" ? { locale: undefined, text: value } : value;
}

export type Browse = { ns?:number; name:String; }
export function toBrowse( path:string, defaultNs:Ns|undefined ):Browse{
	const segments = path.split("/");
	if( segments.length>1 )
		return toBrowse( segments[segments.length-1], defaultNs );
	const text = segments[0];
	const index = text.indexOf( "~" );
	if( index>0 ){
		const nsStr = text.substring( 0, index );
		if( /^-?\d+$/.test(nsStr) )
			return { ns: +nsStr, name: text.substring( index + 1 ) };
	}
	return { ns: defaultNs, name: text };
}
export function browseEq( a:Browse, b:Browse ):boolean{ return a.ns===b.ns && a.name===b.name; }

//Namespace 0's DataTypes by id, as Part 6 names them - the node page's and the Members tab's type names - and Folder, the FolderType.
export enum ETypes{
	None = 0,
	Boolean = 1,
	SByte = 2,
	Byte = 3,
	Int16 = 4,
	UInt16 = 5,
	Int32 = 6,
	UInt32 = 7,
	Int64 = 8,
	UInt64 = 9,
	Float = 10,
	Double = 11,
	String = 12,
	DateTime = 13,
	Guid = 14,
	ByteString = 15,
	XmlElement = 16,
	NodeId = 17,
	ExpandedNodeId = 18,
	StatusCode = 19,
	QualifiedName = 20,
	LocalizedText = 21,
	Structure = 22,
	DataValue = 23,
	BaseDataType = 24,
	DiagnosticInfo = 25,
	Number = 26,
	Integer = 27,
	UInteger = 28,
	Enumeration = 29,
	Folder = 61,
	Duration = 290,
	UtcTime = 294,
	ServerState = 852
}