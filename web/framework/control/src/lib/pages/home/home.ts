import { inject } from '@angular/core';
import { P } from "@angular/cdk/keycodes";
import { Component, OnInit, signal } from "@angular/core";
import { ActivatedRoute, Router } from "@angular/router";
import { ComponentCategoryList, RouteItem, IROUTE_SERVICE, IRouteService, isNotFound } from "jde-spa";

@Component( {
	templateUrl: './home.html',
	styleUrls: ['./home.scss'],
	imports: [ComponentCategoryList]
})
export class Home implements OnInit {
	private route:ActivatedRoute = inject( ActivatedRoute );
	private router:Router = inject( Router );
	private routerService:IRouteService = inject( IROUTE_SERVICE );
	async ngOnInit(){
		let items = new Array<RouteItem>();
		for( let config of this.router.config.filter(x=> x.data && x.path!.length && x.path!="login" && !isNotFound(x)) )
			items.push( new RouteItem({title: config.title as string, path: config.path!, /*id: config.path,*/ summary: config.data!["pageSettings"]?.summary}) );
		this.items.set( items );
	}

	items = signal<RouteItem[]>( null as any );
}