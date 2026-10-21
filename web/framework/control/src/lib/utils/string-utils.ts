
export class StringUtils{
	//capitalize, uncapitalize, plural and singular are jde-spa's (text.ts)
	//GraphQL string literals share JSON's escape grammar (\" \\ \b \f \n \r \t \uXXXX), so JSON.stringify IS the correct escaper here - a hand-built `"${value}"` corrupts the query on any quote, backslash, or newline in user text.
	static qlString( value:string ):string{ return JSON.stringify( value ); }
	static idToDisplay( id:string ){ //TODO change function name
		let y=''
		for( let i=0; i<id.length; ++i )
		{
			let ch = id[i];
			if( i>0 )
			{
				if( ch==ch.toUpperCase() )
					y+=" ";
				y+=ch;
			}
			else
				y+=ch.toUpperCase();
		}
		return y;
	}
}