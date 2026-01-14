#!/bin/bash
cd "$(dirname "$0")"

export POSTGRES_URI="${POSTGRES_URI:-postgres://postgres:password@localhost:5432/postgres}"

dotnet build -c Release
dotnet bin/Release/net10.0/RealWorld.dll
