import { StatusCode } from "./types";

//OPC 10000-4 7.38 (Tables 176/177):  31:30 Severity, 27:16 SubCode, 15 StructureChanged, 14 SemanticsChanged,
//11:10 InfoType (01 = DataValue, which is what gives 9:0 a meaning), 9:8 LimitBits, 7 Overflow, 4:0 historian bits (not shown).
//Every shift is `>>>`:  a StatusCode is a uint32 and JS bit operators answer in int32, so `>>` and a bare `&` go negative for every Bad code.
export enum ESeverity{ Good, Uncertain, Bad, Reserved }
export enum ELimit{ None, Low, High, Constant }
export type InfoBits = { limit:ELimit, overflow:boolean, structureChanged:boolean, semanticsChanged:boolean };

export function severity( sc:StatusCode|undefined ):ESeverity{ return ( (sc ?? 0)>>>30 ) as ESeverity; }
export function isBad( sc:StatusCode|undefined ):boolean{ return ( (sc ?? 0)>>>31 )==1; }//the reserved 11 included, as UA_StatusCode_isBad has it

//the code a name belongs to - severity + sub-code, flags off.  /ErrorCodes names a code by its top 16 bits (UA_StatusCode_name),
//so one entry serves every InfoBits variant of it.  `>>>0`:  without it a Bad key is negative, the server can't parse it out of
//`?scs=`, no name ever comes back and the pending fetch repeats forever.
export function nameKey( sc:StatusCode ):StatusCode{ return ( sc & 0xFFFF0000 )>>>0; }

export function infoBits( sc:StatusCode|undefined ):InfoBits{
	const x = sc ?? 0;
	const dataValue = ( (x>>>10) & 3 )==1;
	return {
		limit: dataValue ? ( (x>>>8) & 3 ) as ELimit : ELimit.None,
		overflow: dataValue && ( x & 0x80 )!=0,
		structureChanged: ( x & 0x8000 )!=0,//15 and 14 sit outside InfoType
		semanticsChanged: ( x & 0x4000 )!=0
	};
}
//the emulator's spelling and order (Quality.cpp ToString), so the screen and its status line read the same
export function flagsText( sc:StatusCode|undefined ):string{
	const bits = infoBits( sc );
	return ( bits.limit ? `+${ELimit[bits.limit]}` : "" ) + ( bits.overflow ? "+Overflow" : "" ) + ( bits.structureChanged ? "+StructureChanged" : "" ) + ( bits.semanticsChanged ? "+SemanticsChanged" : "" );
}
export function scHex( sc:StatusCode ):string{ return "0x"+( sc>>>0 ).toString( 16 ).toUpperCase().padStart( 8, "0" ); }

//`name` is the server's text for nameKey(sc), when it is known.  Good needs none - which covers a flagged Good (0x00000480) too.
export function statusText( sc:StatusCode|undefined, name?:string ):string{
	const x = sc ?? 0;
	const key = nameKey( x );
	const base = key==0 ? "Good" : name || `${ESeverity[severity(x)]} ${scHex(key)}`;//the severity and the code, until the name arrives
	return base+flagsText( x );
}
//a shape per severity, so the colour is never the only signal.  Plain Good has no icon.
export function statusIcon( sc:StatusCode|undefined ):"error"|"warning"|"info"|undefined{
	if( !sc )
		return undefined;
	const s = severity( sc );
	return s>=ESeverity.Bad ? "error" : s==ESeverity.Uncertain ? "warning" : "info";
}