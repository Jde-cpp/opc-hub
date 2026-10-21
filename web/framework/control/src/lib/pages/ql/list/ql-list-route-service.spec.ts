import { TestBed } from '@angular/core/testing';
import { provideRouter, Routes, UrlSegment } from '@angular/router';
import { QLListRouteService } from './ql-list-route-service';
import { Blank } from 'jde-spa/testing';

const routes:Routes = [
	{ path: 'access', title: 'Access', component: Blank },
	{ path: 'access', component: Blank, children: [
		{ path: 'users/:slug', component: Blank },
		{ path: 'audit', title: 'Audit', component: Blank, data: {summary: 'Who changed what'} },
		{ path: ':collectionDisplay', component: Blank, data: { collections: [
			{ path: 'users', data: {icon: 'person', summary: 'People', tableSettings: {canAdd: false}} },
			{ path: 'roles', title: 'Role sets' },
			'groups'
		]} }
	]}
];

describe( 'QLListRouteService', ()=>{
	const create = ()=>{
		TestBed.configureTestingModule({ providers: [provideRouter(routes), QLListRouteService] });
		return TestBed.inject( QLListRouteService );
	};

	it( 'names each collection behind :collectionDisplay as its list does, and keeps the plain children', async ()=>{
		const items = await create().docItems( [new UrlSegment('access', {})] );
		expect( items.map( i=>[i.path, i.title] ) ).toEqual( [['audit', 'Audit'], ['users', 'Users'], ['roles', 'Role sets'], ['groups', 'Groups']] );
		expect( items[1] ).toMatchObject( {icon: 'person', summary: 'People', cardClass: 'card-access'} );
		expect( items[0].summary ).toBe( 'Who changed what' );
	});
});