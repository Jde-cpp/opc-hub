#pragma once

namespace Jde::DB{struct IDataSource;}
extern "C"{
	Jde::DB::IDataSource* GetDataSource();
}
