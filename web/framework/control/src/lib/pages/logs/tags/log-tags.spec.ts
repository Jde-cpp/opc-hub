import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { ELogLevel } from 'jde-proto/Log';
import { LogTags, TagLevel } from './log-tags';

const override = ( tag:string, level:string ):TagLevel=>({ tag, level, override: true });
const configured = ( tag:string, level:string ):TagLevel=>({ tag, level, override: false });
const make = ( tags:TagLevel[] )=>{
	const fixture = TestBed.createComponent( LogTags );
	fixture.componentRef.setInput( 'tags', tags );
	fixture.componentRef.setInput( 'catalogue', ['default', 'sql', 'socket', 'sessions'] );
	fixture.detectChanges();
	return fixture.componentInstance;
};
const row = ( tags:LogTags, tag:string )=>tags.dataSource.find( r=>r.tag==tag )!;

describe( 'LogTags.entries', ()=>{
	beforeEach( ()=>TestBed.configureTestingModule({}) );

	//angular-review3 #10: the default row is SYNTHESIZED when nothing is stored (logTags.h falls back to Information).
	//Reporting it as an override made LogSettingsPanel.save diff `undefined != 'Information'` and write a tag-0/Information
	//row for all three sinks on every Save - pinning the instance's default over its configured level, across restarts.
	it( 'omits a synthesized default the user never touched', ()=>{
		const tags = make( [override("sql", "Debug")] );
		expect( tags.entries() ).toEqual( {sql: "Debug"} );
	} );

	it( 'omits it even when the sink has no stored tags at all', ()=>{
		expect( make([]).entries() ).toEqual( {} );
	} );

	it( 'reports a default the instance really stored', ()=>{
		expect( make([override("default", "Warning"), override("sql", "Debug")]).entries() ).toEqual( {default: "Warning", sql: "Debug"} );
	} );

	//choosing Information IS a change when the configured default is unknown, so a touched row always counts.
	it( 'reports the default once the user picks a level, Information included', ()=>{
		const tags = make( [] );
		tags.onLevelChange( row(tags, "default"), ELogLevel.Information );
		expect( tags.entries() ).toEqual( {default: "Information"} );
	} );

	it( 'reports a changed default level', ()=>{
		const tags = make( [] );
		tags.onLevelChange( row(tags, "default"), ELogLevel.Warning );
		expect( tags.entries() ).toEqual( {default: "Warning"} );
	} );

	it( 'still skips the trailing empty row', ()=>{
		const tags = make( [override("sql", "Debug")] );
		expect( tags.dataSource.some(r=>!r.tag) ).toBe( true );
		expect( Object.keys(tags.entries()) ).toEqual( ["sql"] );
	} );
} );

//install-issues #19: the page showed the stored overrides - `Default: Info` on an install - not the levels the instance runs
//with.  The panel now hands over both, and the editor has to show a configured level without reporting it as an override.
describe( 'LogTags configured levels', ()=>{
	beforeEach( ()=>TestBed.configureTestingModule({}) );

	it( 'shows a configured tag at its running level and does not report it', ()=>{
		const tags = make( [configured("default", "Trace"), configured("sessions", "Trace"), override("sql", "Debug")] );
		expect( row(tags, "default").level ).toBe( ELogLevel.Trace );
		expect( row(tags, "sessions") ).toMatchObject( {level: ELogLevel.Trace, override: false, configured: ELogLevel.Trace} );
		expect( tags.isConfigured(row(tags, "sessions")) ).toBe( true );
		expect( tags.isConfigured(row(tags, "sql")) ).toBe( false );
		expect( tags.entries() ).toEqual( {sql: "Debug"} );
	} );

	it( 'makes a configured tag an override when its level changes, and not when it is set back', ()=>{
		const tags = make( [configured("sessions", "Trace")] );
		tags.onLevelChange( row(tags, "sessions"), ELogLevel.Warning );
		expect( tags.entries() ).toEqual( {sessions: "Warning"} );
		tags.onLevelChange( row(tags, "sessions"), ELogLevel.Trace );
		expect( tags.entries() ).toEqual( {} );
	} );

	it( 'treats picking the running default level as no override', ()=>{
		const tags = make( [configured("default", "Debug")] );
		tags.onLevelChange( row(tags, "default"), ELogLevel.Debug );
		expect( tags.entries() ).toEqual( {} );
		tags.onLevelChange( row(tags, "default"), ELogLevel.Warning );
		expect( tags.entries() ).toEqual( {default: "Warning"} );
	} );

	it( 'reverts an overridden configured tag on delete instead of dropping the row', ()=>{
		const tags = make( [configured("sessions", "Trace")] );
		const sessions = row( tags, "sessions" );
		tags.onLevelChange( sessions, ELogLevel.Warning );
		tags.onDelete( sessions );
		expect( row(tags, "sessions") ).toMatchObject( {level: ELogLevel.Trace, override: false} );
		expect( tags.entries() ).toEqual( {} );
	} );

	it( 'drops a stored override on delete', ()=>{
		const tags = make( [override("sql", "Debug")] );
		tags.onDelete( row(tags, "sql") );
		expect( tags.dataSource.some(r=>r.tag=="sql") ).toBe( false );
		expect( tags.entries() ).toEqual( {} );
	} );

	it( 'keeps a stored override an override whatever level is picked', ()=>{
		const tags = make( [override("sql", "Debug")] );
		tags.onLevelChange( row(tags, "sql"), ELogLevel.Trace );
		expect( tags.entries() ).toEqual( {sql: "Trace"} );
	} );

	it( 'offers a configured tag to no new row', ()=>{
		const tags = make( [configured("sessions", "Trace")] );
		expect( tags.selectableTags(tags.dataSource.find(r=>!r.tag)!) ).toEqual( ['sql', 'socket'] );
	} );
} );

//reviews/m3-closing.md #30:  the Default row never had the remove button, and after a Save it is always a stored override - so
//`default: null` (install-issues #21, ClearDefaultLevel) was unreachable:  the override was re-applied at every start, masking
//the configured default, with no way back but the database.
describe( 'LogTags removing a Default override', ()=>{
	beforeEach( ()=>TestBed.configureTestingModule({}) );
	//pick:  a level chosen on the Default row's picker, as the page does it - its event, not a direct call, which marks no view dirty
	const buttonOn = ( tags:TagLevel[], pick?:ELogLevel )=>{
		const fixture = TestBed.createComponent( LogTags );
		fixture.componentRef.setInput( 'tags', tags );
		fixture.componentRef.setInput( 'catalogue', ['default', 'sql'] );
		fixture.detectChanges();
		if( pick!==undefined ){
			fixture.debugElement.query( By.css('mat-row severity-picker') ).triggerEventHandler( 'levelChange', pick );
			fixture.detectChanges();
		}
		const first = (fixture.nativeElement as HTMLElement).querySelector( 'mat-row' )!;//the Default row leads
		return first.querySelector( 'button mat-icon' )?.textContent?.trim();
	};

	it( 'offers the button on the Default row when it is an override', ()=>{
		expect( buttonOn([override("default", "Warning")]) ).toBe( "undo" );//the row stays - it is reverted, not deleted
		expect( buttonOn([configured("default", "Trace")], ELogLevel.Warning) ).toBe( "undo" );
		expect( buttonOn([configured("default", "Trace")]) ).toBeUndefined();//nothing to remove
	} );

	it( 'keeps the row when a stored Default override is removed, and the save then clears it', ()=>{
		const given = [override("default", "Warning"), override("sql", "Debug")];
		const tags = make( given );
		tags.onDelete( row(tags, "default") );
		expect( row(tags, "default") ).toMatchObject( {override: false, removed: true} );
		expect( tags.entries() ).toEqual( {sql: "Debug"} );//stored {default, sql} less this - the save diff sends default: null
		expect( tags.selectableTags(tags.dataSource.find(r=>!r.tag)!) ).not.toContain( "default" );
		tags.onLevelChange( row(tags, "default"), ELogLevel.Error );//changed its mind
		expect( row(tags, "default") ).toMatchObject( {override: true, removed: false} );
		expect( tags.entries() ).toEqual( {default: "Error", sql: "Debug"} );
	} );

	it( 'resets a Default the user picked but nothing stored in place', ()=>{
		const tags = make( [] );
		tags.onLevelChange( row(tags, "default"), ELogLevel.Warning );
		tags.onDelete( row(tags, "default") );
		expect( row(tags, "default") ).toMatchObject( {level: ELogLevel.Information, override: false} );
		expect( row(tags, "default") ).not.toMatchObject( {removed: true} );//nothing stored, nothing to remove on save
		expect( tags.entries() ).toEqual( {} );
	} );
} );
