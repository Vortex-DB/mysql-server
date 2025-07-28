if [ -z $1 ] 
then
  echo "Usage: $0 [configuration file]"
  exit 1
fi

sudo /usr/local/mysql/bin/mysqld_safe --defaults-file=$1 &> ./mysqld_safe.log &

