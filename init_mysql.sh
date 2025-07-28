if [ -z $1 ] 
then
  echo "Usage: $0 [configuration file]"
  exit 1
fi

/usr/local/mysql/bin/mysqld --defaults-file=$1 --initialize &> ~/logs/mysqld_safe.log &

