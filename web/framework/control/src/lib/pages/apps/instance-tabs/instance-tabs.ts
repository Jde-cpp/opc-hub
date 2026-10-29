import { Component, DestroyRef, Directive, TemplateRef, contentChildren, effect, inject, input, model } from '@angular/core';
import { NgTemplateOutlet } from '@angular/common';
import { MatTabsModule } from '@angular/material/tabs';
import { HelpAnchor } from 'jde-spa';
import { AppService } from '../../../services/app/app-service';
import { IGraphQL } from '../../../services/graphql';
import { LogDetail } from '../../logs/detail/log-detail';
import { LogSettingsPanel } from '../../logs/settings/log-settings-panel';
import { WebSessions } from '../web-sessions/web-sessions';

//A page's own tab ahead of the shared ones:  `<ng-template instanceTab="Connections">…</ng-template>`.  instanceTabHelp names its section
//in the page's help topic, which the navbar's ? button opens while the tab is selected.
@Directive( {selector: 'ng-template[instanceTab]'} )
export class InstanceTab{
	label = input.required<string>( {alias: 'instanceTab'} );
	help = input<string>( undefined, {alias: 'instanceTabHelp'} );
	template = inject( TemplateRef );
}

//The tab group the three application-instance pages share - app-server, opc-server and gateway:  each page's own tabs
//(InstanceTab), then Logs, Log Settings and Web Sessions.  A whole group rather than the three tabs alone, because a
//mat-tab-group only finds the tabs written in its own template, never ones a child component renders.
@Component( {
	selector: 'instance-tabs',
	templateUrl: './instance-tabs.html',
	host: {style: 'display: contents'},//the group lays out as the page's child, as it did when each page wrote it
	imports: [MatTabsModule, NgTemplateOutlet, LogDetail, LogSettingsPanel, WebSessions]
})
export class InstanceTabs{
	logs = input.required<IGraphQL|undefined>();//the instance's own service; the Logs tab is built again when it changes
	instanceId = input<number>();//the instance's registration with the application server, which answers Log Settings and Web Sessions
	noun = input<string>( "server" );//what the instance is, for the tabs' no-registration line
	selectedIndex = model<number>( 0 );
	tabs = contentChildren( InstanceTab );
	appService = inject( AppService );
	constructor(){
		const anchor = inject( HelpAnchor );
		effect( ()=>anchor.fragment.set( this.tabs()[this.selectedIndex()]?.help() ) );
		inject( DestroyRef ).onDestroy( ()=>anchor.fragment.set( undefined ) );
	}
}