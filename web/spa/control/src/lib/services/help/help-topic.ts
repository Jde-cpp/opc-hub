import { Injectable, InjectionToken, signal } from '@angular/core';
import { ParamMap } from '@angular/router';

//One entry in the help section.  `url` is the markdown asset, relative so it resolves against <base href>;  `routes` are the
//route-path patterns the navbar's ? button maps onto the topic - ':x' matches any segment, and a pattern matches as a PREFIX
//of the url, so 'access' covers everything under /access.  `routes: ['']` marks the fallback topic.
export interface HelpTopic{
	id:string;//its url under /help:  one segment, or two for a page split out of another's ('gateways/groups', /help/:topic/:subtopic)
	title:string;
	summary?:string;
	icon?:string;
	url:string;
	routes?:string[];
}
//jde-spa owns the help page and the navbar but sits below the libraries that own the content, so the topics reach it through
//this multi token - the SEARCH_PROVIDERS pattern.  Registration order is display order.
export const HELP_TOPICS = new InjectionToken<HelpTopic[][]>( 'HelpTopics' );

export function helpTopics( injected:HelpTopic[][]|null ):HelpTopic[]{ return (injected ?? []).flat(); }
//A topic's page as a router link:  its id's segments apart, as one segment would carry its '/' as %2F
export function helpTopicRoute( topic:HelpTopic ):string[]{ return ['/help', ...topic.id.split( '/' )]; }
//The id a /help/:topic or /help/:topic/:subtopic url asks for
export function helpTopicId( params:ParamMap ):string{ return [params.get( 'topic' ), params.get( 'subtopic' )].filter( s=>!!s ).join( '/' ); }

//The section of the url's topic the navbar's ? button opens - a heading's '#slug' in the topic's markdown, set by whatever shows a
//part of the page the topic has a section for (InstanceTabs, from its selected tab) and unset when that goes.
@Injectable( {providedIn: 'root'} )
export class HelpAnchor{
	readonly fragment = signal<string|undefined>( undefined );
}

//A card on /help that leaves the site - the issue tracker - rather than opening a topic.  Its own token, not a HelpTopic:
//the help page's side list, the navbar's ? button and search all expect a markdown page behind a topic's id.  Multi, in
//display order, after the topics.
export interface HelpLink{
	title:string;
	summary?:string;
	icon?:string;
	href:string;
}
export const HELP_LINKS = new InjectionToken<HelpLink[][]>( 'HelpLinks' );

//The topic for a url.  The pattern spelling out the most literal segments wins ('access' over 'gateways/:g/:c' for
///access/users/alice, as route-utils' matchLiterals reasons), then the longer pattern, then registration order.
export function helpTopicFor( topics:HelpTopic[], segments:string[] ):HelpTopic|undefined{
	let best:HelpTopic|undefined;
	let bestScore = -1;
	for( const topic of topics ){
		for( const pattern of topic.routes ?? [] ){
			const parts = pattern.split( '/' ).filter( s=>s.length );
			if( parts.length>segments.length || !parts.every( (p,i)=>p.startsWith(':') || p==segments[i] ) )
				continue;
			const score = parts.filter( p=>!p.startsWith(':') ).length*100 + parts.length;
			if( score>bestScore ){
				best = topic;
				bestScore = score;
			}
		}
	}
	return best;
}