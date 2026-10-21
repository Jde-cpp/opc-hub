import { TestBed } from '@angular/core/testing';
import { IENVIRONMENT } from 'jde-spa';
import { ELogLevel } from 'jde-proto/Log';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { LogSettingsPanel, mergeSink, overridesOf, saveHint } from './log-settings-panel';
import { snackbarStub } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';

//install-issues #19: a sink's tab is the union of what the instance reports running (logSetting, tag->level) and what is
//stored for it (instanceTagLevel's rows, flattened) - the stored set marks the overrides, the instance sets the level.
describe( 'LogSettingsPanel.mergeSink', ()=>{
	it( 'shows the running levels, marking the tags that have a stored override', ()=>{
		const rows = mergeSink( {default: "Trace", sessions: "Trace", sql: "Warning"}, {sql: "Warning"} );
		expect( rows ).toEqual( [
			{ tag: "default", level: "Trace", override: false },
			{ tag: "sessions", level: "Trace", override: false },
			{ tag: "sql", level: "Warning", override: true }
		] );
	} );

	it( 'reads an applied override at the level the instance runs it', ()=>{
		expect( mergeSink({sql: "Warning"}, {sql: "Debug"}) ).toEqual( [{ tag: "sql", level: "Warning", override: true }] );
	} );

	it( 'falls back to the stored level for an override the instance does not report', ()=>{
		expect( mergeSink({default: "Information"}, {sql: "Debug"}) ).toEqual( [
			{ tag: "default", level: "Information", override: false },
			{ tag: "sql", level: "Debug", override: true }
		] );
	} );

	it( 'is the stored overrides alone when the instance did not answer', ()=>{
		expect( mergeSink(undefined, {default: "Warning", sql: "Debug"}) ).toEqual( [
			{ tag: "default", level: "Warning", override: true },
			{ tag: "sql", level: "Debug", override: true }
		] );
	} );

	it( 'puts default first and the rest in shown-name order', ()=>{
		const rows = mergeSink( {"read.server.socket": "Debug", app: "Trace", default: "Information", sql: "Trace"}, {} );
		expect( rows.map(r=>r.tag) ).toEqual( ["default", "app", "read.server.socket", "sql"] );//Sock.Server.Read files under S, not R
	} );
} );

//install-issues #20: the toolbar read "Changes take effect when the instance restarts" for a save that is pushed to the
//running instance at once and re-read at every start.  The restart wording is only right for an instance that did not answer.
describe( 'LogSettingsPanel.saveHint', ()=>{
	it( 'says a change is live and kept when the instance answered', ()=>{
		expect( saveHint(true) ).toMatch( /at once/ );
		expect( saveHint(true) ).toMatch( /next start/ );
		expect( saveHint(true) ).not.toMatch( /restart/ );
	} );

	it( 'says a change waits for the next start only when the instance did not answer', ()=>{
		expect( saveHint(false) ).not.toMatch( /at once/ );
		expect( saveHint(false) ).toMatch( /next starts/ );
	} );
} );

describe( 'LogSettingsPanel.overridesOf', ()=>{
	it( 'is what the editor reports untouched, so an unedited Save diffs to nothing', ()=>{
		const rows = mergeSink( {default: "Trace", sessions: "Trace", sql: "Warning"}, {sql: "Warning"} );
		expect( overridesOf(rows) ).toEqual( {sql: "Warning"} );
		expect( overridesOf(mergeSink({default: "Trace"}, {})) ).toEqual( {} );
	} );
} );


//A14:  the three tabs come from one list, and Save reads every tab's editor - the two not shown included - by its place in it.
describe( 'LogSettingsPanel', ()=>{
	const create = async ()=>{
		const querySingle = vi.fn( async ( q:string )=>q.startsWith( 'logSetting' )
			? {tags: {sql: 1, app: 1}}
			: {text: {Debug: ['sql']}, binary: {}, appServer: {}, running: {text: {default: 'Information', sql: 'Debug'}, binary: {default: 'Warning'}, appServer: {}}} );
		const mutate = vi.fn( async ()=>({}) );
		TestBed.configureTestingModule({ providers: [
			{ provide: SnackbarService, useValue: snackbarStub() },
			{ provide: IENVIRONMENT, useValue: {get: ()=>true} }
		]});
		const fixture = TestBed.createComponent( LogSettingsPanel );
		fixture.componentRef.setInput( 'service', {querySingle, mutate} );
		fixture.componentRef.setInput( 'instanceId', 7 );
		await stable( fixture, 2 );
		return { fixture, panel: fixture.componentInstance, querySingle, mutate };
	};

	it( 'gives each sink a tab, and says which one the instance does not run', async ()=>{
		const { fixture, panel, querySingle } = await create();
		expect( querySingle.mock.calls[1][0] ).toContain( '{ binary appServer text running }' );
		const labels = [...fixture.nativeElement.querySelectorAll( '.mdc-tab__text-label' )].map( l=>(l as Element).textContent!.trim() );
		expect( labels ).toEqual( ['Binary', 'Remote', 'Text'] );
		expect( panel.sinks().map( s=>s.note ) ).toEqual( [null, 'This instance runs no remote log. The rows are overrides saved for it; they apply to nothing.', null] );
	} );

	it( 'saves the edits of every tab, not only the one shown', async ()=>{
		const { panel, mutate } = await create();
		const [binary, , text] = panel.editors();
		binary.onLevelChange( binary.dataSource[0], ELogLevel.Error );
		text.onDelete( text.dataSource.find( r=>r.tag=='sql' )! );
		await panel.save();
		expect( mutate ).toHaveBeenCalledTimes( 1 );
		expect( (mutate.mock.calls[0] as any[])[0].args ).toEqual( {
			binary: [{tags: ['default'], level: 'Error'}],
			text: [{tags: ['sql'], level: null}]
		} );
	} );
} );