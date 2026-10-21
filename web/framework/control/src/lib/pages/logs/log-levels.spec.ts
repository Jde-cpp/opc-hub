import { ELogLevel } from 'jde-proto/Log';
import { levelFromWire, levelToWire, logLevel, logLevels } from './log-levels';

describe( 'log levels', ()=>{
	it( 'lists the six levels the server logs at, in order', ()=>{
		expect( logLevels.map( l=>l.level ) ).toEqual( [ELogLevel.Trace, ELogLevel.Debug, ELogLevel.Information, ELogLevel.Warning, ELogLevel.Error, ELogLevel.Critical] );
		expect( logLevel(ELogLevel.Warning) ).toMatchObject( {name: 'Warning', short: 'Wrn', cssClass: 'chip-warn'} );
		expect( logLevel(ELogLevel.NoLog) ).toBeUndefined();
	} );
	it( 'spells a level on the wire by its name, and both off values as None', ()=>{
		for( const l of logLevels )
			expect( levelFromWire(levelToWire(l.level)) ).toBe( l.level );
		expect( levelToWire(ELogLevel.NoLog) ).toBe( 'None' );
		expect( levelToWire(ELogLevel.LogLevelNone) ).toBe( 'None' );
		expect( levelFromWire('None') ).toBe( ELogLevel.NoLog );
		expect( levelFromWire('Loud') ).toBe( ELogLevel.Information );
	} );
} );