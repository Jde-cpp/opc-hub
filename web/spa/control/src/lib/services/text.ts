//Word-shaping every library needs, here at the bottom of the dependency chain so jde-spa can use it too.
export function capitalize( value:string ):string{ return value.charAt(0).toUpperCase()+value.slice(1); }
export function uncapitalize( value:string ):string{ return value.charAt(0).toLowerCase()+value.slice(1); }//a type name's field or collection form:  OpcServer -> opcServer
//`noun` with an `s` it does not already end in - none for a count `n` of 1
export function plural( noun:string, n?:number ):string{ return n==1 || noun.endsWith('s') ? noun : noun+'s'; }
export function singular( noun:string ):string{ return noun.endsWith('s') ? noun.slice(0,-1) : noun; }