import { Component, input, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { AppService } from '../../../services/app/app-service';
import { IGraphQL } from '../../../services/graphql';
import { LogDetail } from '../../logs/detail/log-detail';
import { LogSettingsPanel } from '../../logs/settings/log-settings-panel';
import { WebSessions } from '../web-sessions/web-sessions';
import { InstanceTab, InstanceTabs } from './instance-tabs';
import { stable } from 'jde-spa/testing';

//reviews/angular-refactor.md A5:  app-server, opc-server and gateway each wrote these tabs, and the keyed `@for` in each, by hand.
@Component( {selector: 'log-detail', template: 'logs'} )
class LogsStub{ service = input<unknown>(); }
@Component( {selector: 'log-settings', template: 'settings {{ instanceId() }}'} )
class SettingsStub{ service = input<unknown>(); instanceId = input<number>(); }
@Component( {selector: 'web-sessions', template: 'sessions'} )
class SessionsStub{ service = input<unknown>(); instanceId = input<number>(); }

@Component( {
	imports: [InstanceTab, InstanceTabs],
	template: `<instance-tabs [logs]="logs" [instanceId]="instanceId()" noun="gateway" [(selectedIndex)]="index">
		<ng-template instanceTab="Connections"><p class="mine">{{ name }}</p></ng-template>
		<ng-template instanceTab="Templates">templates</ng-template>
	</instance-tabs>`
})
class Host{
	logs = {} as IGraphQL;
	name = "the page's own";
	instanceId = signal<number|undefined>( 7 );
	index = signal( 0 );
}

describe( 'InstanceTabs', ()=>{
	const create = async ( index:number, instanceId?:number )=>{
		TestBed.resetTestingModule();//a test builds two
		TestBed.configureTestingModule({ providers: [{ provide: AppService, useValue: {} }] });
		TestBed.overrideComponent( InstanceTabs, {remove: {imports: [LogDetail, LogSettingsPanel, WebSessions]}, add: {imports: [LogsStub, SettingsStub, SessionsStub]}} );
		const fixture = TestBed.createComponent( Host );
		fixture.componentInstance.index.set( index );
		fixture.componentInstance.instanceId.set( instanceId );
		await stable( fixture );
		return fixture.nativeElement as HTMLElement;
	};
	const body = ( root:HTMLElement )=>root.querySelector( '.mat-mdc-tab-body-active' )!.textContent!.trim();

	it( "puts the page's tabs ahead of the shared three", async ()=>{
		const root = await create( 0, 7 );
		expect( [...root.querySelectorAll('.mdc-tab__text-label')].map( l=>l.textContent!.trim() ) ).toEqual( ["Connections", "Templates", "Logs", "Log Settings", "Web Sessions"] );
		expect( root.querySelector( '.mat-mdc-tab-body-active .mine' )?.textContent ).toBe( "the page's own" );//rendered in the page's context
	} );

	it( 'hands the registration to Log Settings', async ()=>{
		expect( body(await create( 3, 7 )) ).toBe( "settings 7" );
	} );

	it( 'says so where the instance has no registration', async ()=>{
		expect( body(await create( 3 )) ).toBe( "No application instance registered for this gateway — log settings are unavailable." );
		expect( body(await create( 4 )) ).toBe( "No application instance registered for this gateway — web sessions are unavailable." );
	} );
} );