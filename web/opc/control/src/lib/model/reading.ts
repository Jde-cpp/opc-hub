import { DatePipe } from '@angular/common';
import Long from 'long';
import { ProtoUtils, Timestamp } from 'jde-framework';

//a read's value as nodes( opc, id ){ value } answers it:  the reading, unwrapped from an Uncertain {v, sc}, or undefined with the
//status for a Bad read ({sc} alone) - Value::ToJson's rule, never to drop a status
export const reading = ( value:unknown ):{ value?:unknown; sc?:number }=>{
	if( value && typeof value=='object' && !Array.isArray(value) ){
		const o = value as Record<string,unknown>;
		if( o.hasOwnProperty('v') )
			return { value: o['v'], sc: o['sc'] as number|undefined };
		if( o.hasOwnProperty('sc') )
			return { sc: o['sc'] as number };
	}
	return { value };
};
//a count as the gateway writes it, a number or a UInt64's Long form {low, high, unsigned}, as text;  blank for none - the Server tab's
//and the Open Protocol row's `_Op`
export const countText = ( v:unknown ):string=>{
	if( typeof v=='number' )
		return String( v );
	if( v && typeof v=='object' && 'low' in v && 'high' in v )
		return new Long( Number((v as any).low), Number((v as any).high), !!(v as any).unsigned ).toString();
	return v==null ? "" : String( v );
};
//a DateTime as the gateway writes it, {seconds, nanos}, or a date's text, in the pipe's locale - the Server tab's and `_Op`'s;  blank for
//none, the text as it came for one that is no date
export const dateText = ( v:unknown, pipe:DatePipe ):string=>{
	if( v && typeof v=='object' && typeof (v as {seconds?:unknown}).seconds=='number' ){
		const d = ProtoUtils.toDate( { nanos: 0, ...v } as Timestamp );
		return d ? pipe.transform( d, 'medium' ) ?? "" : "";
	}
	if( typeof v!='string' || !v )
		return "";
	const d = new Date( v );
	return isNaN( d.getTime() ) ? v : pipe.transform( d, 'medium' ) ?? v;
};