import { StatusCode } from "./types";

//OPC 10000-4 7.38 (Tables 176/177):  31:30 Severity, 27:16 SubCode, 15 StructureChanged, 14 SemanticsChanged,
//11:10 InfoType (01 = DataValue, which is what gives 9:0 a meaning), 9:8 LimitBits, 7 Overflow, 4:0 the historian bits of
//Part 11 §6.4.5 (Table 20):  1:0 Raw, Calculated or Interpolated, 2 Partial, 3 ExtraData, 4 MultiValue - a history read's,
//an aggregate's above all.
//Every shift is `>>>`:  a StatusCode is a uint32 and JS bit operators answer in int32, so `>>` and a bare `&` go negative for every Bad code.
export enum ESeverity{ Good, Uncertain, Bad, Reserved }
export enum ELimit{ None, Low, High, Constant }
export enum EHistorian{ Raw, Calculated, Interpolated }
export type InfoBits = { limit:ELimit, overflow:boolean, structureChanged:boolean, semanticsChanged:boolean, historian:EHistorian, partial:boolean, extraData:boolean, multiValue:boolean };

export function severity( sc:StatusCode|undefined ):ESeverity{ return ( (sc ?? 0)>>>30 ) as ESeverity; }
export function isBad( sc:StatusCode|undefined ):boolean{ return ( (sc ?? 0)>>>31 )==1; }//the reserved 11 included, as UA_StatusCode_isBad has it

//the code a name belongs to - severity + sub-code, flags off.  /ErrorCodes names a code by its top 16 bits (UA_StatusCode_name),
//so one entry serves every InfoBits variant of it.  `>>>0`:  without it a Bad key is negative, the server can't parse it out of
//`?scs=`, no name ever comes back and the pending fetch repeats forever.
export function nameKey( sc:StatusCode ):StatusCode{ return ( sc & 0xFFFF0000 )>>>0; }

export function infoBits( sc:StatusCode|undefined ):InfoBits{
	const x = sc ?? 0;
	const dataValue = ( (x>>>10) & 3 )==1;
	const historian = x & 3;
	return {
		limit: dataValue ? ( (x>>>8) & 3 ) as ELimit : ELimit.None,
		overflow: dataValue && ( x & 0x80 )!=0,
		structureChanged: ( x & 0x8000 )!=0,//15 and 14 sit outside InfoType
		semanticsChanged: ( x & 0x4000 )!=0,
		historian: dataValue && historian<3 ? historian as EHistorian : EHistorian.Raw,//3 is reserved
		partial: dataValue && ( x & 0x4 )!=0,
		extraData: dataValue && ( x & 0x8 )!=0,
		multiValue: dataValue && ( x & 0x10 )!=0
	};
}
//the emulator's spelling and order (Quality.cpp ToString), so the screen and its status line read the same, then the historian
//bits that qualify a value:  Interpolated, a derived value, not a reading;  Partial, an interval cut short;  ExtraData and
//MultiValue, that a record hides others.  Not Calculated, which every aggregate is, and not Raw, which every reading is.
export function flagsText( sc:StatusCode|undefined ):string{
	const bits = infoBits( sc );
	return ( bits.limit ? `+${ELimit[bits.limit]}` : "" ) + ( bits.overflow ? "+Overflow" : "" ) + ( bits.structureChanged ? "+StructureChanged" : "" ) + ( bits.semanticsChanged ? "+SemanticsChanged" : "" )
		+ ( bits.historian==EHistorian.Interpolated ? "+Interpolated" : "" ) + ( bits.partial ? "+Partial" : "" ) + ( bits.extraData ? "+ExtraData" : "" ) + ( bits.multiValue ? "+MultiValue" : "" );
}
export function scHex( sc:StatusCode ):string{ return "0x"+( sc>>>0 ).toString( 16 ).toUpperCase().padStart( 8, "0" ); }

//`name` is the server's text for nameKey(sc), when it is known.  Good needs none - which covers a flagged Good (0x00000480) too.
export function statusText( sc:StatusCode|undefined, name?:string ):string{
	const x = sc ?? 0;
	const key = nameKey( x );
	const base = key==0 ? "Good" : name || `${ESeverity[severity(x)]} ${scHex(key)}`;//the severity and the code, until the name arrives
	return base+flagsText( x );
}
//a shape per severity, so the colour is never the only signal.  Plain Good has no icon, nor has a Good that only says it was
//calculated, every aggregate's.
export function statusIcon( sc:StatusCode|undefined ):"error"|"warning"|"info"|undefined{
	if( !sc )
		return undefined;
	const s = severity( sc );
	return s>=ESeverity.Bad ? "error" : s==ESeverity.Uncertain ? "warning" : nameKey( sc ) || flagsText( sc ) ? "info" : undefined;
}