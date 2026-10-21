import { diffIds, Mutation, MutationType } from "jde-framework";
import { Permission } from "./permission";
import { Role } from "./role";

//An acl row joins an identity - a user or a group - to a role, or carries an identity's permission of its own.
export class Acl {
	//an identity's acl as the server answers it, split into its roles and its own permissions
	static split( acl:{role?:unknown, permissionRight?:unknown}[] ):{roles:Role[], permissions:Permission[]}{
		const roles:Role[] = [], permissions:Permission[] = [];
		for( const member of acl ){
			if( member.role )
				roles.push( new Role(member.role) );
			else if( member.permissionRight )
				permissions.push( new Permission(member.permissionRight) );
		}
		return { roles, permissions };
	}
	//The rows to purge and to create that take `original`'s ids to `modified`'s;  `row( id )` names the identity and the role the
	//id's row joins - the id is the role for an identity's page, the identity for a role's.
	static mutations( original:readonly {id:number}[]|undefined, modified:readonly {id:number}[]|undefined, row:( id:number )=>{identity:{id:number}, role:{id:number}} ):Mutation[]{
		const {added, removed} = diffIds( original, modified );
		return [...removed.map( id=>new Mutation(Acl.typeName, undefined, row(id), MutationType.Purge) ), ...added.map( id=>new Mutation(Acl.typeName, undefined, row(id), MutationType.Create) )];
	}
	static roleMutations( identityId:number, original:Role[], modified:Role[] ):Mutation[]{
		return Acl.mutations( original, modified, id=>({identity: {id: identityId}, role: {id}}) );
	}
	static typeName = "Acl";
}