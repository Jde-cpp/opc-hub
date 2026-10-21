import { ELogLevel } from 'jde-proto/Log';

//The levels the server logs at, each once:  `name` labels a pick list, `short` the log table's level chip and `cssClass` its
//tone (_general.scss) - none for trace, which keeps the theme's.
export type LogLevelInfo = { level:ELogLevel, name:string, short:string, cssClass:string };
export const logLevels:readonly LogLevelInfo[] = [
	{ level: ELogLevel.Trace, name: "Trace", short: "Trc", cssClass: "" },
	{ level: ELogLevel.Debug, name: "Debug", short: "Dbg", cssClass: "chip-neutral" },
	{ level: ELogLevel.Information, name: "Information", short: "Inf", cssClass: "chip-ok" },
	{ level: ELogLevel.Warning, name: "Warning", short: "Wrn", cssClass: "chip-warn" },
	{ level: ELogLevel.Error, name: "Error", short: "Err", cssClass: "chip-error" },
	{ level: ELogLevel.Critical, name: "Critical", short: "Crt", cssClass: "chip-critical" }
];
export function logLevel( level:ELogLevel ):LogLevelInfo|undefined{ return logLevels.find( l=>l.level==level ); }
//a tag's level as the log settings carry it:  the enum's name, and "None" for either of the two off values
export function levelToWire( level:ELogLevel ):string{ return level==ELogLevel.NoLog || level==ELogLevel.LogLevelNone ? "None" : ELogLevel[level]; }
export function levelFromWire( wire:string ):ELogLevel{ return wire=="None" ? ELogLevel.NoLog : (<any>ELogLevel)[wire] ?? ELogLevel.Information; }