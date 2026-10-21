import { capitalize, plural, singular, uncapitalize } from 'jde-spa';

export class MetaObject{
	constructor( public type:string ){} //User
	get collectionDisplay(){ return this.type+'s'; }
	get collectionName(){ return MetaObject.toCollectionName(this.type); }
	get singular(){ return uncapitalize( this.type ); }
	get idReferenceName(){ return this.singular+"Id"; }
	static toTypeFromCollection( collectionName:string ){ return singular( capitalize(collectionName) ); }
	static toCollectionName(type:string):string{ return plural( uncapitalize(type) ); }
}