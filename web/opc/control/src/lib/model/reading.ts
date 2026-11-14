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